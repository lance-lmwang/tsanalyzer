# TsAnalyzer Pro: v3 System Architecture

TsAnalyzer v3 is designed as a **Three-Plane Architecture**, separating packet processing, timing analysis, and control orchestration. This separation ensures deterministic throughput while maintaining flexibility for large-scale deployments.

---

## 1. Core Design Philosophy

1.  **Plane Isolation**: The **Data Plane** is strictly decoupled from the **Control Plane**. Slow control operations (e.g., generating a large JSON report) are never allowed to stall packet ingestion.
2.  **NUMA Locality First**: Packet data and processing state must never cross physical CPU socket boundaries (zero QPI/UPI latency).
3.  **Lock-Free Data Plane**: 0 mutex, 0 malloc, 0 blocking in the hot path. All packets move via Single-Producer Single-Consumer (SPSC) ring buffers.
4.  **Streams are State Machines**: A stream is not a thread; it is a compact state machine multiplexed inside a fixed number of **Reactor Threads**.
5.  **Branchless Logic**: Eliminate conditional branches in the TS parser to maximize CPU pipeline efficiency.

---

## 2. Dual-Mode Y-Architecture

TsAnalyzer is designed with a unique **Y-Architecture** to satisfy both "plug-and-play" enterprise monitoring needs and "highly customizable" edge gateway requirements.

### Mode A: Static Pipeline (The Analyzer)
Traditional, hardcoded C-pipeline exposed via tools like `tsa_server_pro`. Pre-wired routing (Input -> Analyzer -> Exporter). Rigid but extremely stable.

### Mode B: Dynamic Lua Pipeline (The Gateway)
Embeds a Lua JIT/VM into the Control Plane. Primitives (Source, Analyzer, Output) are exposed as Lua userdata objects for flexible topological linking.

---

## 3. Global System Architecture

The architecture utilizes a tiered fan-out model to scale to **100 Gbps** and **1000+ streams**:

```text
                 +--------------------------------------+
                 |        Control Plane (REST/gRPC)     |
                 |  - Stream configuration / Lua VM     |
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
|   | - Jitter modeling   |   | - T-STD simulation |                |
|   +----------+----------+   +----------+----------+               |
+--------------+-------------------------+--------------------------+
               |
               v
+-------------------------------------------------------------------+
|                        Data Plane                                  |
|                                                                   |
| +--------------------+    +----------------------+                |
| | NIC Capture        | -> | SIMD TS Parser       | -> SPSC Ring   |
| | (DPDK / AF_PACKET) |    | (AVX2 / SSE4.2)      |                |
| +--------------------+    +----------------------+                |
+-------------------------------------------------------------------+
```

---

## 4. Layered Implementation Stack

| Layer | Responsibility | Implementation Primitives |
| :--- | :--- | :--- |
| **4. Interface** | External Comms | JSON, Prometheus, `tsa_top` TUI, **JWT Gateway**. |
| **3. Metrology** | Simulation | ETSI TR 101 290, 3D PCR Math, **Predictive T-STD**. |
| **2. Structural** | Protocol | Multi-standard SI/PSI, 27MHz STC, NALU Sniff. |
| **1. Ingestion** | Physical | **HAT (Hardware Timing)**, recvmmsg, SPSC Rings. |

---

## 5. High-Scale Scheduling: The 1000-Stream Model

To scale to industrial limits, TsAnalyzer uses a **Hybrid Partitioning Model**:

### 5.1 Stage 1: Hardware-Driven NUMA Distribution
Using **NIC RSS (Receive Side Scaling)** or **Flow Steering**, incoming traffic is bifurcated at the hardware level. Each physical CPU socket (NUMA node) receives its own dedicated slice of traffic, ensuring packets never cross the Inter-Connect (QPI/UPI).

### 5.2 Stage 2: Software-Driven Worker Affinity
*   **Balancing**: `Worker_ID = Consistent_Hash(Stream_ID) % Local_Workers`.
*   **Cache Locality**: Ensures a specific stream's state (PCR PLL, VBV buffers) always resides in the same core's **L1/L2 cache**, eliminating cross-core cache invalidation cycles.

---

## 6. TS Processing Pipeline (8-Stage O(1))

The per-packet analytical pipeline is strictly **O(1)** to maintain deterministic latency.

1.  **NIC Ingress**: Hardware RX Timestamping.
2.  **Packet Classification**: Stream Hash & Flow identification.
3.  **TS Header Parser**: SIMD-accelerated PUSI/PID/CC extraction.
4.  **Continuity Audit**: Sequence verification per PID.
5.  **PCR Processor**: Software PLL & 3D jitter metrology.
6.  **Bitrate Model**: Piecewise constant bitrate estimation.
7.  **TR 101 290 Engine**: State machine updates (P1/P2/P3).
8.  **Metrics Bus**: Atomic push to the Reactor Core.

---

## 7. Backpressure & Congestion Policy

*   **Drop Strategy**: Supports `drop_head` (lowest latency) and `drop_tail`.
*   **Broadcast Awareness**: In extreme congestion, the gateway performs **NULL PID Stripping** (PID 0x1FFF) to recover bandwidth without impacting content.
*   **Stream Priority**: "Platinum" streams can preempt resources from lower-tier variants.

---

## 8. High-Performance Execution Primitives

### 8.1 Hardware Abstraction Layer (HAL) & SIMD
*   **AVX2**: Scans 32 bytes in a single cycle (~32x faster than scalar).
*   **SSE4.2**: Optimized fallback for legacy cloud environments.

### 8.2 Metrics Cardinality Management
*   **Strict Labeling**: High-cardinality metadata is restricted to the REST API.
*   **Summarization**: Prometheus uses stable labels (`service_id`, `node_id`) to prevent TSDB index explosion.

---

## 9. Future Roadmap: GPU-Accelerated QoE

TsAnalyzer will leverage **NVDEC/NVENC** for ultra-high-density video auditing:
*   **Visual QC**: Offloading black/freeze detection and thumbnail generation to GPU Tensor cores.
*   **AI Metadata**: Utilizing deep learning for logo verification and BITC OCR correlation.

---

## 10. Asynchronous Signaling & Failure Containment

1.  **Signaling Thread**: Offloads Webhook/Lua callbacks from the Data Plane.
2.  **Hot-Reload**: Uses Linux `inotify` for hitless configuration updates.
3.  **Metrology Watchdog**: Monitors thread heartbeats and can forcefully reset the Lua VM while keeping the C Data Plane operational.

---

## 11. Implementation Roadmap: The 100Gbps Challenge (Track 021)

TsAnalyzer is undergoing a rigorous **Scalability Stress Assessment**:
*   **Load-vs-Latency Curve**: Modeling the impact of cache pressure on 3D PCR jitter accuracy.
*   **Ring Buffer Optimization**: Dynamic depth adjustment based on NIC burst characteristics.
