# TsAnalyzer Technical Specification

## 1. High-Level Architecture
TsAnalyzer is a carrier-grade, distributed Transport Stream **Metrology Gateway**. It operates on a **Modular Reactor-Worker** architecture, decoupling I/O (Ingress), Processing (Analyzers), and Action (Conditioning/Gateway).

## 2. Runtime Execution Model (Threading)
To ensure deterministic timing and zero-drop performance, the threading model is strictly partitioned:

### 2.1 Threading Hierarchy
1.  **IO Reactor Threads (Ingress)**:
    - Dedicated threads for SRT `epoll`, UDP `recvmsg`, and Zixi/RIST/HLS polling.
    - Responsibility: Raw packet capture, non-blocking socket management, and immediate handoff.
2.  **Ingress Thread (Data Ingest)**:
    - Dedicated to packet timestamping (IngressClock), hardware/kernel timestamp extraction, and TS framing.
    - Responsibility: Ensures every packet has a high-precision arrival time before entering the Ring Buffer.
3.  **Worker Pool (Processing Layer)**:
    - Affinity-bound threads executing the **Processing Graph**.
    - Responsibility: Each worker maximizes L2/L3 cache locality by handling a specific subset of streams for metrology and QoE analysis.
4.  **Action & Egress Thread (Output)**:
    - Dedicated to the Pacer and Output Drivers.
    - Responsibility: Ensures absolute-time scheduled packet release via `clock_nanosleep`.
5.  **Alert & Signaling Thread (NOC)**:
    - Specialized thread for the Webhook engine.
    - Responsibility: Manages HTTP retries, cooldown timers, and JSON serialization without impacting the data plane.

### 2.2 System Execution Flow
```text
                        +----------------------+
                        |   Control Plane      |
                        |----------------------|
                        | REST API / Prometheus|
                        | Config Hot Reload    |
                        | Lua Scriptable Ctrl  |
                        +----------+-----------+
                                   |
                                   v
+-------------------------------------------------------------+
|                     IO Reactor Layer                        |
|-------------------------------------------------------------|
| UDP Listener | SRT Listener | RIST | Zixi | HTTP/HLS Pull   |
| Non-blocking sockets (epoll/kqueue)                         |
+-----------------------------+-------------------------------+
                              |
                              v
                    +---------------------+
                    |   Ingress Thread    |
                    |---------------------|
                    | Packet Timestamping |
                    | NIC / Kernel TS     |
                    | TS Framing          |
                    +----------+----------+
                               |
                               v
                     (Lock-Free SPSC Ring)
                               |
                               v
+----------------------------------------------------------------+
|                      Worker Processing Pool                    |
|----------------------------------------------------------------|
| Stream Context                                                 |
|                                                                |
|  +------------------ Passive Analyzer Taps ----------------+  |
|  |                                                          |  |
|  |  Metrology        Compliance        QoE        Essence   |  |
|  |  ---------        ----------        ---        -------   |  |
|  |  PCR PLL          TR101290          Freeze     NALU DPI  |  |
|  |  Jitter           PAT/PMT           Black      SPS/PPS   |  |
|  |  Drift            CC errors         Silence    SCTE-35   |  |
|  |                                                          |  |
|  +----------------------------------------------------------+  |
|                                                                |
|                +----------------------------+                  |
|                | Mutating Transformation    |                  |
|                |----------------------------|                  |
|                | CC Repair                  |                  |
|                | PCR Restamp                |                  |
|                | CBR Shaping                |                  |
|                +-------------+--------------+                  |
|                              |                                 |
+------------------------------+---------------------------------+
                               |
                               v
                       Egress Pacing Engine
                 (clock_nanosleep absolute timing)
                               |
                               v
+---------------------------------------------------------------+
|                       Output Drivers                          |
|---------------------------------------------------------------|
| UDP Multicast | SRT Push | RIST | Recording | Local Monitor   |
+---------------------------------------------------------------+

                               |
                               v
                     Async Alert & Telemetry
            +--------------------------------------+
            | Alert State Machine                  |
            | OFF → FIRING → RESOLVED              |
            |                                      |
            | Webhook Dispatcher                   |
            | Metrics Export (Prometheus)          |
            +--------------------------------------+
```

## 3. Memory Architecture (The Data Plane)
Dynamic memory allocation (`malloc/free`) is strictly prohibited in the steady-state data path.
- **Zero-Copy Architecture**: TS packets reside in a pre-allocated **188-Byte Packet Pool**. Only metadata pointers pass through the pipeline stages.
- **Burst Buffers**: IO threads collect packets in bursts (e.g., 7 TS packets per UDP frame) and write them directly into the Slab via zero-copy DMA or `memcpy`.
- **Reference Counting**: Multiple Analyzer Taps (Metrology, QoE, etc.) access packets via reference-counted metadata objects to ensure zero-copy processing.
- **Slab Allocation**: All internal trackers, metrics buffers, and state structures are pre-allocated at startup to ensure zero heap fragmentation.
- **Lock-Free Handoff**: Inter-thread communication uses SPSC buffers with **C11 Acquire/Release** memory barriers.

## 4. Clock Domain Model
TsAnalyzer maintains four independent clock domains to isolate and diagnose impairments:
- **SystemClock (Wall)**: Physical reference (CLOCK_MONOTONIC). Used for **Physical Bitrate** (Total TS Bitrate) calculation via a minimum 500ms sampling window.
- **IngressClock (Arrival)**: Kernel or hardware timestamps recorded at the NIC ingest point.
- **PCRClock (Reference)**: The reconstructed 27MHz timeline. Used for **Business Bitrate** (PCR-Content) calculation per program.
- **MediaClock (PTS/DTS)**: The 90KHz media timeline extracted from PES headers for A/V synchronization and skew audit.
- **Metrology Integrity**: To prevent 10x calculation errors, the engine strictly forbids mixing domains (e.g., dividing bits by STC ticks to get bps) within a single tier. Each tier samples its own reference domain independently.

## 5. Multi-tenancy & Routing Logic
### 5.1 Hierarchical Namespace (Taxonomy)
Streams utilize a structured ID system to support ABR and multi-tenant isolation:
- **Format**: `{tenant}/{application}/{service}/{variant}` (e.g., `cloud-a/live/cctv1/hd`).
- **Merge Logic**: Stream instances inherit parameters via an **"Is-Set" bitmask**. Only explicitly defined directives override inherited VHost baselines.
- **Persistence**: Dynamic stream states are serialized to `tsa_state.json` for reboot survival.
- **Modularity**: Supports `include conf.d/*.conf` for modular file management.

### 5.2 Ingress Dispatch (Decoupled Push/Pull)
The system dispatches drivers based on URI patterns:
- **Passive (Push)**: Waiting for a client to push to `srt://{global_port}?streamid={stream_id}`.
- **Active (Pull)**: Proactively connecting to `udp://`, `srt://`, `http://` (HLS), `zixi://`, or `rist://`.

### 5.3 Routing Priority
Incoming connections are matched using:
1.  **Exact Match**: Full match of the hierarchical `StreamID` string.
2.  **Prefix Pattern**: Match the tenant-based prefix (e.g., `tenant/*`).
3.  **Program Mapping**: Selective PID analysis for MPTS via `program_number` filter.
4.  **Fallback**: Default to `__default__` VHost.

## 6. Processing Pipeline Model (Graph-based)
Each stream is executed as a dynamically assembled graph of specialized **Stages**.
- **Passive Taps (Analyzers)**: Metrology, Compliance, QoE, and Essence modules operate in "tap" mode via shared packet pointers. They are isolated to prevent logic failures from impacting forwarding.
- **Mutating Stages (Transformation)**: CC Repair, PCR Restamp, and CBR Pacer stages are allowed to modify packet headers or insert/drop packets.
- **Execution Efficiency**: Stages are linked via a function pointer chain at runtime. Disabled stages are physically removed, ensuring **Zero CPU Cost** for unused features.

## 7. The 5-Layer Observation Model (Analyzers)
### 7.1 Layer 1: Metrology (Physical & Temporal)
- **High-Precision Bitrate**:
    - **Physical Tier**: Total TS Bitrate derived from engine-synchronized unique packet counts. Includes PID+CC de-duplication to handle PCAP loopback duplicates.
    - **Business Tier**: Multi-program (MPTS) aware bitrate calculation. Per-program rates are summed to report global throughput without program collision.
- **Clock Analysis**: Software PLL sync (< 10ns precision), Jitter Decomposition (AC/DR/OJ), HLS fetch latency vs. media segment duration, manifest drift tracking, Network MDI (DF/MLR), and IAT Histograms.
- **T-STD Simulation**: Real-time Annex D compliant mathematical model for TB, MB, and EB buffers. Includes **Predictive Underflow** logic that anticipates buffer starvation up to 500ms before it impacts the decoder.

### 7.2 Layer 2: Compliance (ISO/Standard)
- TR 101 290 (P1, P2, P3), MPTS structure verification, and cross-table (PAT/PMT/SDT) consistency check.
- **Alert State Machine**: Stateful tracking: `OFF` -> `FIRING` -> `RESOLVED` (with a configurable 5s stability window).
- **Alert Suppression Tree**: Root-cause inhibition where a `SYNC_LOSS` event automatically silences downstream errors (CC, PCR, Timeout) to eliminate alert storms.

### 7.3 Layer 3: QoE (Sensory & Artifacts)
- Visual (Black screen, Freeze, Macroblocking) and Audio (Silence, Loss) detection.
- **Entropy Analysis**: Real-time Shannon entropy implementation to distinguish valid frozen content from encrypted noise or black screens.

### 7.4 Layer 4: Essence (Payload & Metadata)
- Zero-copy NALU sniffing (SPS/PPS/GOP), SCTE-35/104 extraction, and Closed Caption presence auditing.
- **SCTE-35 Alignment Audit**: Nanosecond-level verification of `splice_insert` command placement relative to the nearest IDR frame.

### 7.5 Layer 5: Scripting (Dynamic Pipeline)
- **Programmable Topology**: Lua-based scripting engine allowing users to dynamically define the processing graph (`input -> analyzer -> output`).
- **Reactive Routing**: Real-time event hooks in Lua enable custom failover logic, PID-level filtering, and conditional routing based on stream health or metadata events.

## 8. Action Engine & Egress (Gateway)
### 8.1 Transformation (Conditioning)
- **CC Repair**: Intelligent NULL packet insertion to maintain Continuity Counter integrity.
- **PCR Re-stamping**: Overwriting incoming PCRs with jitter-free values from the internal PLL to eliminate network jitter.
- **CBR Pacing**: Absolute-time scheduled packet release for perfectly flat output using `clock_nanosleep`.
### 8.2 Protection & Multi-Output
- **Multi-Output**: Concurrent egress drivers for `udp://`, `srt://`, `rist://`, and `record://`.
- **Failover**: Intelligent switching (`MasterHealth < Threshold`) with `discontinuity_indicator` injection.
- **Backpressure**: Configurable `drop_old` or `block` policy during egress saturation.
- **Global Quota**: Total aggregate bitrate limit to prevent hardware exhaustion.
- **Recording**: Triggered micro-captures (pre-roll ring buffer saved to `.ts` on error).

## 9. Reliability & Signaling
### 9.1 Webhook Engine
- **Reliability**: 1024-deep queue, 3 retries, exponential backoff (1s, 2s, 4s).
- **Noise Control**: Root-cause inhibition (Sync loss silences sub-alerts) and **10s aggregation window**.
- **PID Granularity**: Alert filtering supports specific PID exclusion/inclusion.
### 9.2 Operations
- **Hot-Reload**: Two-phase validation (Shadow Tree) and atomic pointer swap for hitless updates.
- **Cloud-Native**: Dedicated lightweight `/health` endpoint and JSON structured logs.

## 10. Metrics Model (Prometheus Namespaces)
- `tsa_metrology_*`: `pcr_jitter_ns`, `pcr_drift_ppm`, `iat_delta_ns`.
- `tsa_compliance_*`: `cc_error_total`, `pat_timeout_count`, `sync_loss_total`.
- `tsa_qoe_*`: `freeze_events_total`, `black_detect_bool`, `av_sync_offset_ms`.
- `tsa_pipeline_*`: `egress_bitrate_bps`, `pacer_dropped_packets`.
- `tsa_system_*`: `worker_cpu_usage`, `ring_buffer_fill_pct`, `alert_queue_depth`.

## 11. Configuration Units
- **Bitrate**: `bps`, `Kbps`, `Mbps`, `Gbps`.
- **Time**: `us`, `ms`, `s`, `min`, `h`.
- **Data Size**: `B`, `K`, `M`, `G`.
- **Boolean**: `on/off`, `true/false`, `yes/no`.
