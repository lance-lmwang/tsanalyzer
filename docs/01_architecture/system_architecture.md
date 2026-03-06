# TsAnalyzer Pro: v3 System Architecture

TsAnalyzer v3 is designed as a **Three-Plane Architecture**, separating packet processing, timing analysis, and control orchestration. This separation ensures deterministic throughput while maintaining flexibility for large-scale deployments.

---

## 1. Core Design Philosophy

1.  **Plane Isolation**: The **Data Plane** is strictly decoupled from the **Control Plane**. Slow control operations (e.g., generating a large JSON report) are never allowed to stall the packet ingestion or analysis worker threads.
2.  **NUMA Locality First**: Packet data and processing state must never cross physical CPU socket boundaries (zero QPI/UPI latency).
3.  **Lock-Free Data Plane**: 0 mutex, 0 malloc, 0 blocking in the hot path. All packets move via Single-Producer Single-Consumer (SPSC) ring buffers.
4.  **Streams are State Machines**: A stream is not a thread; it is a compact state machine multiplexed inside a fixed number of **Reactor Threads**.
5.  **Branchless Logic**: Eliminate conditional branches in the TS parser to maximize pipeline efficiency.

---

## 2. Global System Architecture

The architecture utilizes a tiered fan-out model to scale to **10 Gbps** and **1000+ streams**:

```text
                 +--------------------------------------+
                 |        Control Plane (REST/gRPC)     |
                 |  - Stream configuration              |
                 |  - Monitoring API / Telemetry        |
                 +------------------+-------------------+
                                    |
                                    v
+-------------------------------------------------------------------+
|                        Analysis Plane                              |
|                                                                   |
|   +-------------------+     +--------------------+                |
|   | Timing Engine     |     | Semantic Analyzer  |                |
|   | - PCR reconstruction |  | - TR 101 290 checks|                |
|   | - Jitter modeling   |   | - StatMux detection |               |
|   +----------+----------+   +----------+----------+               |
+--------------+-------------------------+--------------------------+
               |
               v
+-------------------------------------------------------------------+
|                        Data Plane                                  |
|                                                                   |
| +--------------------+    +----------------------+                |
| | NIC Capture        | -> | SIMD TS Parser       | -> SPSC Ring   |
| | (DPDK / AF_XDP)    |    | (AVX2 / AVX-512)     |                |
| +--------------------+    +----------------------+                |
+-------------------------------------------------------------------+
```

---

## 3. Layered Implementation Stack

While the Planes define logical isolation, the engine code is implemented as a 4-layer deterministic pipeline.

| Layer | Responsibility | Implementation Primitives |
| :--- | :--- | :--- |
| **4. Interface** | External Comms | JSON (Bit-exact), Prometheus, `tsa_top` TUI. |
| **3. Metrology** | Simulation | ETSI TR 101 290, 3D PCR Math, T-STD VBV. |
| **2. Structural** | Protocol | Multi-standard SI/PSI, 27MHz STC, NALU Sniff. |
| **1. Ingestion** | Physical | **HAT (Hardware Timing)**, recvmmsg, SPSC Rings. |

---

## 4. The NUMA Pipeline

NUMA awareness is critical for maintaining the **8M pps** data plane throughput.

### 4.1 Data Residency Rule
**Packet data must never cross NUMA nodes.** Crossing the interconnect (QPI/UPI) introduces non-deterministic latency spikes. Each physical CPU socket manages an independent hardware-to-software pipeline.

### 4.2 CPU Layout Example (32-Core Appliance)
*   **NUMA Node 0 (Cores 0-15)**:
    *   Core 0: Ingest RX Worker (NIC0).
    *   Cores 1-15: Analysis Workers (Stream Group A).
*   **NUMA Node 1 (Cores 16-31)**:
    *   Core 16: Ingest RX Worker (NIC1).
    *   Cores 17-31: Analysis Workers (Stream Group B).

### 4.3 RSS Queue & Core Affinity
The Ingestion layer utilizes **Receive Side Scaling (RSS)** based on `UDP Destination IP + Port` to ensure **Same Stream → Same Core** affinity.

---

## 4. TS Processing Pipeline

The per-packet analytical pipeline is strictly **O(1)** to maintain deterministic latency (< 1ms processing delay).

### 4.1 Pipeline Stages
1.  **NIC Ingress**: Hardware RX Timestamping.
2.  **Packet Classification**: Stream Hash & Flow identification.
3.  **TS Header Parser**: Branchless PUSI/PID/CC extraction.
4.  **Continuity Counter Audit**: Sequence verification per PID.
5.  **PCR Processor**: Software PLL & jitter metrology.
6.  **Bitrate Model**: Piecewise constant bitrate estimation.
7.  **TR 101 290 Engine**: State machine updates (P1/P2/P3).
8.  **Metrics Bus**: Atomic push to the Reactor Core.

---

## 5. Reactor Stream Model

v3 treats **Streams as State Machines** multiplexed inside **Reactor Threads**. Each Reactor manages ~100 streams.

### 5.1 Event Loop Logic
The loop performs wait-free polling:
```c
while(running) {
    batch = ring_pop_batch();
    for(pkt in batch) {
        stream = flow_table[pkt.flow];
        process_ts_packet(stream, pkt); // Updates state machines
    }
}
```

### 5.2 Cache Residency
The `ts_stream_t` context is carefully packed to reside within a single cache line (64-128 bytes) where possible, ensuring that all metrology math (PCR PLL, VBV) happens in L1/L2 cache.

---

## 6. High-Performance Execution Primitives

### 6.1 TS SIMD Parser (AVX-512 / AVX2)
Instead of byte-by-byte state machines, v3 uses SIMD vectors to perform "dimensional reduction" on the 188-byte TS packets.
*   **Vectorized Sync Detection**: Uses `_mm512_cmpeq_epi8_mask` (or `_mm256_cmpeq_epi8`) to scan entire cache lines for the `0x47` sync byte in a single instruction.
*   **Header Gathering**: Uses Gather/Shuffle instructions to extract 13-bit PIDs from multiple packets simultaneously.
*   **Register-level Drop**: Non-analyzed PIDs are masked at the register level to prevent unnecessary memory writes.

### 6.2 1000+ Stream Scheduler (Run-to-Completion)
To eliminate OS context-switch overhead:
*   **Worker-per-Core**: A fixed pool of worker threads equals the physical core count.
*   **Time-Wheel QoS**: Employs an **O(1) Time-Wheel algorithm** for deferred analysis tasks (e.g., TR 101 290 P1 timeouts).

---

## 7. Asynchronous Signaling Pipeline

TsAnalyzer v3 offloads all non-deterministic external communication to a dedicated thread pool to ensure core timing integrity.

### 7.1 Dispatch Mechanism
1.  **Metrology Core**: Detects an incident and pushes an `event_t` to the **Signal Queue**.
2.  **Signaling Thread**: Consumes the queue and executes the Webhook POST (non-blocking).
3.  **Isolation**: Prevents network latency spikes during HTTP POSTs from contaminating the 27MHz clock reconstruction.
