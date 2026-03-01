# TsAnalyzer Pro: COTS Software-Based Inline Broadcast Assurance Appliance

`tsa_server_pro` represents a paradigm shift from traditional media servers and offline analyzers. It is engineered as an **Inline Deterministic Inspection Engine**—a system capable of simultaneously performing ultra-stable, low-latency stream forwarding alongside CPU-bound, deep protocol inspection (TR 101 290, nanosecond PCR jitter). 

The ultimate architectural mandate is: **Forwarding stability must never be compromised by analysis complexity.**

## 1. Core Paradigm: The Side-Car Model (Dual-Path)

If deep analysis sits directly in the forwarding path, any CPU saturation will immediately spike latency and cause network drops. To prevent this, the architecture adopts a **Side-Car Bypass Model**:

```text
            ┌───────────────┐
Ingress ───→│  TX Forward   │───→ Egress (O(1) latency, highly stable)
            └───────┬───────┘
                    │ (Best-Effort Pointer Copy / Dual Queue)
                    ▼
            ┌───────────────┐
            │ ANA Pipeline  │───→ Worker Pool (TR101290, PCR Jitter)
            └───────────────┘
```

*   **Forward Path (Primary)**: The main artery. It only handles packet routing and socket writes. It operates in strict $O(1)$ time per packet and is entirely immune to the CPU load of the analysis engine.
*   **Analysis Path (Side-Car)**: The inspection engine operates as a decoupled observer processing a mirrored copy of the traffic. 

## 2. Stream Registry & "Plugin" Architecture

The architecture is designed for high extensibility through a "Plugin" (Provider) style expansion model, allowing the server to support diverse protocols with a unified internal pipeline.

### 2.1 Supported Ingest Providers
*   **SRT Provider**: Managed via a global `srt_eid` (SRT Epoll ID) using `srt_epoll_wait` for pure asynchronous event handling.
*   **UDP Provider**: Integrated into the same I/O loop using non-blocking `recv` calls.
*   **File Provider**: Supported via a dedicated ingest thread that implements a **Software Pacer**. This simulates real-time bitrates, preventing the queues from being overwhelmed by raw disk speeds.
*   **Future SDKs (Zixi/RIST)**: Third-party SDKs can be integrated as independent provider threads that feed packets into the dual-queue pipeline via the same registry-based interface.

## 3. Strict Forwarding Guarantees (Transparent Proxy)

As an inline appliance, trust in the data path is paramount. The forwarding path adheres to strict **Transparent Proxy** rules:
*   **Zero Modification**: Payload bytes, PCR timestamps, and packet sequencing are strictly untouched.
*   **Emission Granularity Consistency**: The system does not artificially aggregate or regroup bursts. If 32 packets are received in a specific burst pattern, they are emitted with the same micro-structure. This prevents the appliance from inadvertently altering downstream Inter-Arrival Time (IAT) behaviors that could affect IRD PCR locks.
*   **Zero-Allocation Data Path**: To prevent millisecond-level latency spikes caused by OS page faults, the Forwarding Path strictly forbids dynamic memory allocation (`malloc`, dynamic vectors). All buffers are pre-allocated at startup via slab allocators.

## 4. High-Performance Data Pipeline: Dual-Queue Topology

To implement the Side-Car model without doubling memory bandwidth overhead, the system utilizes a **Dual-Queue** mechanism for each stream, built upon hardware-optimized SPSC (Single-Producer Single-Consumer) ring buffers.

### 4.1 The Two Queues & Zero-Impact Enqueue
1.  **TX Queue (Egress)**: Small, high-priority queue.
2.  **ANA Queue (Analysis)**: Larger queue dedicated to the Worker Pool. 

The I/O thread must write to both. To ensure the Forward path is not degraded by ANA enqueue failures (e.g., branch mispredictions or head/tail lock contention):
*   **Best-Effort Analysis**: After the TX enqueue, the ANA enqueue is a fast-path pointer copy.
*   **Silent Fast-Fail & State Resync**: If the ANA Queue is full, the fallback path is a single `atomic_inc(internal_drop)` instruction. No logging, no callbacks. Crucially, the Analysis Worker detects this gap via the `internal_drop` counter and gracefully resyncs its TR 101 290 state machines (e.g., Continuity Counter sequences) to prevent false-positive telemetry alerts caused by internal analyzer overload. Forwarding proceeds instantly.

### 4.2 SPSC Hardware Optimizations & Queue Sizing
*   **NUMA-Local Allocation**: The memory for the SPSC queue is allocated strictly on the NUMA node where the I/O thread and its Workers reside.
*   **Cache Line Alignment**: The queue structure utilizes `alignas(64)` memory alignment to eliminate **False Sharing** between the producer and consumer cores.
*   **Anti-Oscillation Sizing**: The ANA queue size is strictly bounded to prevent high-frequency empty/non-empty state oscillation. A capacity of $\ge 128$ packets ensures that the queue absorbs sudden network bursts, smoothing out the Worker thread scheduling interval and preventing L3 cache bouncing.
*   **Batching**: To maximize CPU instruction pipeline efficiency, I/O threads enqueue bursts of packets, and workers dequeue in batches.

## 5. Hyperscale Concurrency: NUMA-Aware Sharding

To handle thousands of concurrent streams at 10Gbps+ speeds, the architecture employs strict **NUMA-Aware Sharding**.

### 5.1 Fully Isolated Shards
*   **NIC RSS Hash**: Traffic is load-balanced at the hardware level to multiple I/O threads via `SO_REUSEPORT` and NIC Receive-Side Scaling.
*   **PCIe Bus Topology**: NIC physical PCIe lanes must be mapped directly to the local CPU's lanes to prevent cross-socket PCIe transaction delays.
*   **NUMA Locality**: Each NUMA node runs a completely independent shard containing:
    *   Its own I/O Thread(s).
    *   Its own TX (Forwarding) Thread(s).
    *   Its own Worker Pool (Analysis).
    *   Node-local memory allocations for SPSC queues and Metrics Registry.
*   This design absolutely eliminates QPI/Infinity Fabric cross-talk and L3 cache miss storms on multi-socket servers.

### 5.2 Push-Based Ready Queues (MPSC) & Double-Checked Edge-Triggering
To avoid the L3 cache destruction caused by workers polling thousands of stream registries, the architecture uses a bounded **Push-Based Ready Queue**. However, simply pushing a Stream ID for every received packet is a catastrophic anti-pattern.

To survive synchronized burst storms across 10,000 streams (e.g., global encoder restarts), the system implements **Double-Checked Edge-Triggered Scheduling**:
*   **State-Driven Push**: The I/O thread only pushes a Stream ID to the Ready Queue when the ANA Queue transitions from `empty` to `non-empty`.
*   **Relaxed Atomic Deduplication**: A full-barrier `exchange` on 10,000 streams simultaneously causes an L3 bandwidth spike. We use a relaxed load first to mitigate RFO (Read-For-Ownership) traffic:
    ```cpp
    if (!stream->scheduled.load(std::memory_order_relaxed)) {
        if (!stream->scheduled.exchange(true, std::memory_order_acq_rel)) {
            ready_queue_push(stream->id);
        }
    }
    ```
    Workers clear this flag *only* after their Time-Slice Quota expires, ensuring steady-state queue updates stay at a safe ~3M pushes/sec globally.

## 6. Bridging the Gap to Hardware Appliances (COTS Metrology)

To compete with FPGA/ASIC-based hardware analyzers using purely Commercial Off-The-Shelf (COTS) software, `tsa_server_pro` strictly controls OS latency domains.

*   **Hardware Timestamping & Kernel Bypass**: `clock_gettime()` introduces random OS scheduler jitter. `tsa_server_pro` demands specific NICs (Mellanox ConnectX / Intel E810) utilizing `SO_TIMESTAMPING` for physical MAC-layer precision. However, to prevent `cmsg` parsing overhead and `sk_buff` allocation limits from destroying the SIMD optimizations at 10Gbps+, extreme performance profiles deploy via **AF_XDP**. AF_XDP provides true zero-copy kernel bypass directly into user-space while preserving integration with standard Linux networking tooling (unlike the heavier DPDK ecosystem).
*   **Decoupled PCR Math**: Because worker threads process packets in chunks, any latency caused by the worker's time-slice must not bleed into metric calculations. PCR Jitter mathematics are absolutely decoupled from worker execution time, driven *entirely* by the immutable PHY-layer hardware timestamps generated at ingress.
*   **SIMD Acceleration**: Heavy deterministic math utilizes hardware intrinsics (SSE4.2 / ARMv8 CRC) to save dozens of cycles per packet.
*   **TSC Time-Based Fairness Quota**: Analysis workers use a strictly enforced **Time-Slice Quota** driven by the CPU's invariant Time Stamp Counter (`rdtsc`). This guarantees deterministic microsecond execution bounds (e.g., 500µs max), ensuring massive UHD streams cannot stall the analysis of small proxy streams.

## 7. Appliance-Grade Observability & Telemetry

A broadcast appliance must provide predictable execution visibility. Beyond standard protocol metrics, `tsa_server_pro` exposes internal telemetry to prove its deterministic behavior:

*   **Predictability Telemetry**: Exposes ANA queue fill ratio histograms, MPSC Ready Queue occupancy percentiles, and Worker slice overrun counters. This allows NOC engineers to verify internal execution jitter mathematically.
*   **Differentiated Drop Metrics**: By incrementing `internal_drop` during Analysis Queue overflows, the system explicitly excludes internal bottlenecks from TR 101 290 CC Error and PCR Jitter calculations, preventing false-positive network alarms.
*   **Link Metric Correlation**: Native integration with `srt_bstats` provides real-time RTT and retransmission rates.

## 8. Deterministic Capacity Modeling

By combining Zero-Allocation forwarding, NUMA isolation, and SIMD acceleration, `tsa_server_pro` transitions from "best-effort software" to a mathematically calculable broadcast appliance.

### 8.1 Theoretical Forwarding Limits (I/O Bound)
Assuming a 25Gbps NIC and standard TS-over-UDP (7 TS packets, ~1400 bytes total):
*   **Max NIC PPS (25Gbps)**: $25 \times 10^9 / (1400 \times 8) \approx 2.23$ Million pps.
*   **Forwarding Cost**: Optimized zero-allocation forwarding costs ~200 CPU cycles per packet.
*   **Single Core Capacity**: A 3.0 GHz core yields $3 \times 10^9 / 200 \approx 15$ Million pps.
*   **Forwarding Bottlenecks**: CPU cost is ~200 cycles/packet ($15$ Mpps per core), making CPU a non-issue for forwarding. The real hardware limits are **NIC RX Ring depth** (which must be tuned $\ge 4096$ with strict IRQ affinity to prevent silent physical drops) and **PCIe Bus Bandwidth** (25Gbps saturates $\approx 3.125$ GB/s, demanding PCIe 4.0 x8 for safe bidirectional full-duplex margins).

### 8.2 Theoretical Analysis Limits (Compute Bound)
Deep inspection (CRC32 via SSE4.2, CC check, PSI state machine, PCR math) costs approximately **120 cycles** per 188-byte TS packet.
*   **1 Gbps TS Rate**: Generates $\approx 664,000$ TS packets/s.
*   **Required Cycles**: $664k \times 120 \approx 79.6$ Million cycles/s.
*   **Core Utilization**: On a 3.0 GHz CPU, 1 Gbps of deep analysis consumes mathematically $\approx 2.6\%$ of a core. Factoring in L1/L2 cache misses and branch mispredictions, a conservative real-world bound is **~0.1 cores per 1 Gbps of TS traffic**.

### 8.3 The Deterministic Capacity Formula
To provision the appliance for a target load, the required worker cores can be modeled as:
$$ \text{Cores}_{\text{req}} = \frac{\text{Total\_TS\_pps} \times \text{Cycles\_per\_packet}}{\text{CPU\_Frequency\_Hz} \times \text{Pipeline\_Efficiency\_Factor}} $$

On a standard dual-socket 64-core server with 25Gbps NICs, the architecture possesses the theoretical compute overhead to perform broadcast-grade TR 101 290 and PCR jitter analysis on the entirety of the 25Gbps pipe, constrained primarily by PCIe bandwidth and strict NUMA memory locality, rather than CPU cycles.

### 8.4 Operating System Tuning Requirements
To truly match hardware appliances, the non-deterministic nature of the Linux kernel must be neutralized. Production deployments require strict boot parameters (`isolcpus`, `nohz_full`, `intel_pstate=disable`, `processor.max_cstate=1`) to prevent System Management Interrupts (SMI), IRQ balancing, and power-saving sleep states from violating the appliance's sub-millisecond latency budget.
