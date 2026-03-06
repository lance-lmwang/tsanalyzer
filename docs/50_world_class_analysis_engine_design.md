# TsAnalyzer: World-Class Metrology & Analysis Engine Design

## 1. Vision & Architecture

The ultimate goal of TsAnalyzer is not merely to "route or parse" streams, but to act as a **Software-Defined Measurement Instrument** capable of replacing million-dollar hardware appliances.

To achieve this, the core analysis capabilities are being decoupled from simple packet counting and elevated into a **Dedicated Metrology Engine**. This engine performs mathematically rigorous calculations strictly aligned with ETSI, ISO, and IETF standards, utilizing Zero-Copy and Lock-Free techniques to maintain 1Gbps deterministic line-rate.

The Metrology Engine is divided into 4 core analytical domains and 1 policy/forensics layer.

---

## 2. Layer 1: Physical & Transport Telemetry (MDI & SRT)

This layer evaluates the network's impact on the delivery of the stream before any protocol decapsulation occurs.

### 2.1 SRT Deep Metrology
TsAnalyzer integrates deeply with the SRT protocol stack, exposing metrics far beyond simple packet counts:
*   **RTT (Round Trip Time) & Flight Size**: Monitors network congestion limits.
*   **Retransmission Rate & Drop Packets**: Determines whether packet loss is due to insufficient bandwidth or inadequate SRT receiver buffer settings.

### 2.2 MDI (Media Delivery Index - RFC 4445) & IAT Profiling
*   **DF (Delay Factor)**: A rigorous measurement of network-induced micro-bursts and jitter. Instead of relying on OS scheduler-polluted system times, DF calculations are strictly bound to **Hardware Timestamping (NIC RX timestamps)** to bypass OS scheduling latency.
*   **MLR (Media Loss Rate)**: Calculated before and after FEC/SRT recovery, exposing both the "raw link quality" and the "post-recovery quality."
*   **IAT (Inter-Arrival Time) Histograms**: Employs real-time statistical histograms to profile the physical network jitter, charting micro-burst intervals (e.g., 1ms vs 10ms density) to prove whether packet loss is a network switch issue or an endpoint buffer exhaustion issue.

---

## 3. Layer 2: Protocol Compliance (TR 101 290 & PCR 3D)

This layer guarantees that the Transport Stream conforms strictly to DVB/MPEG standards.

### 3.1 Strict TR 101 290 P1/P2/P3 Implementation
*   **Comprehensive Coverage**: Not just P1 (Sync Loss, PAT/PMT) and P2 (CRC, Transport Error), but deep implementation of P3 (SI Repetition, Unreferenced PIDs).
*   **State Machine Driven**: All errors are tracked via strict FSMs (e.g., Sync Loss requires 5 consecutive `0x47` to lock and 2 to lose).

### 3.2 3D PCR Topology & Long-Term Walltime Drift (ISO/IEC 13818-1 Annex I)
A unique competitive advantage over basic open-source tools. PCR jitter is mathematically decomposed using a High-Precision Software PLL and Linear Trend models:
1.  **PCR_AC (Accuracy)**: Transmitter clock precision error.
2.  **PCR_DR (Drift Rate) & Walltime Drift**: Clock drift revealing oscillator temperature/aging deviation. Utilizes a **Linear Trend (Linear Regression)** model continuously applied to the difference between system walltime and extracted PCRs to predict the encoder's clock crystal stability over a 24-hour period.
3.  **PCR_OJ (Overall Jitter)**: Phase jitter introduced by network transit.

---

## 4. Layer 3: Content Timing, T-STD Buffer, & Ad-Insertion Auditing

This layer predicts decoder failures and audits deep payload triggers by simulating the recipient's behavior in memory.

### 4.1 NALU/PES Level Sparse Inspection
*   **PES/NALU Profiling**: Operates a zero-copy state machine inside the PES payload to extract video dimensions, frame rates, and I/P/B frame structures (GOP sequences) for H.264/H.265 *without* fully decoding the video.
*   **PTS/DTS Offset Tracking (Lip-Sync)**: Continuously calculates the differential between Video PTS and Audio PTS, anticipating audio-video desync before human viewers notice.

### 4.2 High-Precision SCTE-35 Audit
*   Extracts SCTE-35 (Digital Program Insertion) markers and measures the absolute **PTS Alignment Offset** between the `pts_time` in the splice command and the *actual* PTS of the nearest IDR/I-Frame in the video track, providing nanosecond-precision ad-insertion auditing.

### 4.3 T-STD / VBV Buffer Water-level Simulation & Bitrate Shaping
*   **Leaky Bucket Model**: By projecting the multiplexing rate and decoding timestamps (DTS), it simulates decoder memory fullness in real-time.
*   **RST (Remaining Safe Time)**: Dynamically calculates the time remaining before a Buffer Underflow/Overflow occurs.
*   **Bitrate Smoother (De-jitter Engine)**: *Future capability* to act as a transport gateway, taking wildly jittered TS inputs and actively reshaping the gaps using accurate PCR clocking to output a perfect Constant Bit Rate (CBR) stream.

---

## 5. Layer 4: Policy & Forensics (Alerting & Micro-Capture)

While C handles the heavy mathematical lifting, Lua provides the strategic policy layer for alarms.

### 5.1 Hysteresis & Alarm Lifecycle (Stateful Alerts)
*   Transient network spikes cause metric flapping. The engine implements a strict lifecycle (`Raised -> Active -> Cleared`).
*   *Example*: PCR_OJ must exceed 500ns for 3 consecutive measurements to trigger an alarm, and must stay below 400ns for 2 seconds to clear.

### 5.2 Triggered Micro-Capture (The "Crime Scene" Snapshot)
*   **Lock-Free Ring Buffer**: A high-speed ring buffer constantly maintains the last 500ms of raw TS payloads in memory.
*   **Trigger Mechanism**: Upon detecting a critical P1 violation (e.g., Sync Loss or CC Error), the engine instantly freezes the buffer.
*   **Forensic Output**: Flushes the frozen window to a `.pcap` or `.ts` file and attaches it to the JSON alarm log, allowing broadcast engineers to perform byte-level RCA (Root Cause Analysis) in Wireshark.

---

## 6. Implementation Strategy

To transition to this architecture:
1.  Establish `src/metrology/` containing distinct mathematical modules (`pcr_trend.c`, `t_std_sim.c`, `iat_histogram.c`, `nalu_sniffer.c`).
2.  Build Golden Tests for every metrology module using known `.ts` fixture files.
3.  Expose all deep metrics to the shared memory / Prometheus exporter for high-density Grafana visualization.