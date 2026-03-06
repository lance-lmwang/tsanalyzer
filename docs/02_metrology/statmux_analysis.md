# Statistical Multiplexing (StatMux) Detection

The StatMux Detection Engine is a high-level diagnostic tool used to reverse-engineer the dynamic bitrate allocation strategies of upstream encoders and multiplexers.

---

## 1. Sliding Window Integrators

The engine maintains high-frequency sliding windows to track the instantaneous throughput of every PID.
*   **Window Tiers**: 100ms (Burst detection), 500ms (StatMux cadence), 1s (Average bitrate).
*   **Implementation**: Utilizes a lock-free circular array of byte-counts per PID, integrated into the `Metrology Worker` path.

---

## 2. Null Packet Displacement (NPD)

StatMux efficacy is measured by how aggressively the multiplexer "squeezes" Null Packets (PID 0x1FFF) to accommodate bitrate spikes in active services.
*   **Detection**: The engine tracks the inverse correlation between service bitrate and Null Packet density.
*   **Diagnostic**: If Null Packet density drops below 2% during a service spike, the engine flags a **Multiplex Saturation Warning**, indicating that further bitrate increases will lead to imminent packet loss or T-STD violations.

---

## 3. VBR Correlation & Phase Alignment

A sophisticated StatMux controller often staggers the I-frames of different channels to avoid simultaneous bitrate peaks (Phase Alignment).

### 3.1 Inter-Channel Correlation
TsAnalyzer correlates the GOP structures across all monitored channels:
*   **Peak Collision**: Detection of multiple channels hitting I-frames within the same 100ms window.
*   **Smoothing Efficiency**: Measures how well the aggregate bitrate remains constant despite individual service variability.

### 3.2 Phase Drift Detection
Tracks the temporal shift between the GOP boundaries of different services. Sudden shifts in phase alignment indicate an upstream multiplexer re-synchronization or encoder group failure.

---

## 4. Operational Value

*   **Capacity Planning**: Determining how many additional services can fit in a transponder/link without sacrificing quality.
*   **Vendor Benchmarking**: Quantifying the efficiency of different StatMux algorithms.
*   **Pre-emptive Alarming**: Alerting on "Multiplex Stress" before a single packet is actually lost.
