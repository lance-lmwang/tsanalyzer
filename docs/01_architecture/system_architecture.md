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

## 2. Dual-Mode Y-Architecture

TsAnalyzer is designed with a unique **Y-Architecture** to satisfy both "plug-and-play" enterprise monitoring needs and "highly customizable" edge gateway requirements. Both modes share the exact same high-performance C-core Data Plane, but they diverge in the Control Plane.

### Mode A: Static Pipeline (The Analyzer)
This is the traditional, hardcoded C-pipeline exposed via tools like `tsa_server_pro`, `tsa_cli`, and `tsa_top`.
*   **Best For**: 24/7 compliance monitoring, standard SaaS API endpoints, fixed-function hardware appliances.
*   **Characteristics**: Zero-configuration startup, pre-wired routing (Input -> Analyzer -> Exporter), rigid but extremely stable.

### Mode B: Dynamic Lua Pipeline (The Gateway)
This mode (invoked via `tsanalyzer run script.lua`) embeds a Lua JIT/VM into the Control Plane to enable dynamic, scriptable stream processing.
*   **Best For**: Complex network routing, conditionally filtering PIDs, dynamic business logic (e.g., triggering a failover based on SCTE-35 or PCR loss).
*   **Characteristics**: The C core provides primitives (Source, Analyzer, Output, Section Extractor) as Lua userdata objects. Users write Lua scripts to dynamically instantiate these objects, wire them together (`analyzer:set_upstream(source)`), and define reactive event callbacks (`on_ts_section`).
*   **Userdata Bindings**: Core C primitives are exposed as Lua objects with automatic memory management (`__gc`).
*   **Event Feedback Loop**: Metrology events generated in the C core (e.g., `SYNC_LOSS`, `SCTE35_SPLICE`) are pushed back into Lua via registered callbacks, enabling real-time intelligent routing and self-healing logic.

**Crucially, these two modes can coexist.** A team can use `tsa_server_pro` for their main dashboard telemetry while running a separate `tsanalyzer run failover.lua` instance on the same machine to handle active stream routing.

---

## 3. Hardware Abstraction Layer (HAL) & SIMD Dispatching

To maintain 10Gbps+ throughput across diverse environments, TsAnalyzer employs a runtime-dispatched HAL:

1.  **VTable Dispatching**: Hot-path functions (Sync Search, PID Extraction) are accessed via a global `tsa_simd` vtable.
2.  **Runtime Detection**: At startup, the engine probes CPU capabilities (`CPUID`) and links the optimal implementation:
    *   **AVX2**: Optimized for modern Xeon/EPYC servers (~32x faster than scalar).
    *   **SSE4.2**: Fallback for legacy hardware and standard cloud VMs (~16x faster than scalar).
    *   **Generic C99**: Portable fallback for non-x86 or restricted environments.

---

## 4. Global System Architecture

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

## 5. Layered Implementation Stack

While the Planes define logical isolation, the engine code is implemented as a 4-layer deterministic pipeline.

| Layer | Responsibility | Implementation Primitives |
| :--- | :--- | :--- |
| **4. Interface** | External Comms | JSON (Bit-exact), Prometheus, `tsa_top` TUI. |
| **3. Metrology** | Simulation | ETSI TR 101 290, 3D PCR Math, T-STD VBV. |
| **2. Structural** | Protocol | Multi-standard SI/PSI, 27MHz STC, NALU Sniff. |
| **1. Ingestion** | Physical | **HAT (Hardware Timing)**, recvmmsg, SPSC Rings. |

---

## 6. Detailed Metrology Logic

### 6.1 T-STD Predictive Buffer Model
TsAnalyzer implements a real-time mathematical simulation of the **ISO/IEC 13818-1 System Target Decoder (T-STD)**.
*   **Predictive Underflow**: By calculating the ingress byte rate vs. the next frame's DTS, the engine predicts buffer starvation up to 500ms before it occurs.
*   **Alert Suppression**: Implements a dependency tree where a `SYNC_LOSS` event automatically suppresses downstream errors (CC, PCR, Timeout) to eliminate alert storms.

### 6.2 Advanced Essence (L4) Analysis
*   **Closed Caption Monitoring**: Continuously audits EIA-608/708 presence within SEI NAL units.
*   **SCTE-35 Alignment**: Cross-references Splice Info Sections with video IDR frames to ensure splicing occurs precisely at GOP boundaries.
*   **Visual QoE (Shannon Entropy)**: Uses information density analysis to distinguish between valid frozen content and encrypted noise or black screens.

---

## 7. The NUMA Pipeline

NUMA awareness is critical for maintaining the **8M pps** data plane throughput.

### 7.1 Data Residency Rule
**Packet data must never cross NUMA nodes.** Crossing the interconnect (QPI/UPI) introduces non-deterministic latency spikes. Each physical CPU socket manages an independent hardware-to-software pipeline.

### 7.2 CPU Layout Example (32-Core Appliance)
*   **NUMA Node 0 (Cores 0-15)**:
    *   Core 0: Ingest RX Worker (NIC0).
    *   Cores 1-15: Analysis Workers (Stream Group A).
*   **NUMA Node 1 (Cores 16-31)**:
    *   Core 16: Ingest RX Worker (NIC1).
    *   Cores 17-31: Analysis Workers (Stream Group B).

---

## 8. Asynchronous Signaling Pipeline

TsAnalyzer v3 offloads all non-deterministic external communication to a dedicated thread pool to ensure core timing integrity.

### 8.1 Dispatch Mechanism
1.  **Metrology Core**: Detects an incident and pushes an `event_t` to the **Signal Queue**.
2.  **Signaling Thread**: Consumes the queue and executes the Webhook POST (non-blocking).
3.  **Hot-Reload**: Uses Linux `inotify` to detect configuration file changes, allowing instant telemetry updates without packet loss.
