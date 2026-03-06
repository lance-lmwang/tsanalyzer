# GOP Structure Analyzer

The GOP Structure Analyzer inspects video elementary streams (H.264/HEVC) inside the MPEG-TS to detect encoder structural anomalies that cause decoder instability and channel-switching delays.

---

## 1. GOP Fundamentals & Monitoring

The analyzer maps every frame detected by the `NALU Sniffer` into a temporal GOP model.

| Parameter | Description | Operational Impact |
| :--- | :--- | :--- |
| **GOP Length (N)** | Distance between I-frames. | High N = Slow zapping; Low N = High bitrate. |
| **IDR Interval** | Physical time between random access points. | Essential for ABR switching alignment. |
| **B-Frame Depth** | Maximum count of consecutive B-frames. | High depth increases decoder latency. |
| **Structure Stability** | Variance in the I-B-P pattern. | Irregularity indicates encoder buffer stress. |

---

## 2. Detection Metrics & Alarms

| Metric | Alarm Trigger | Root Cause |
| :--- | :--- | :--- |
| **GOP Length Variance** | $\sigma > 10\%$ | Unstable encoder rate control. |
| **Missing IDR** | Interval > 2x Target | Severe encoder bug or frame drop. |
| **Excessive B-Frames** | Count > 8 | Non-compliant profile for real-time delivery. |
| **Reference Discontinuity** | Gap in POC | Malformed GOP structure. |

---

## 3. Parsing Pipeline

1.  **PES Assembly**: Reconstructs the ES payload from TS packets.
2.  **NAL Parser**: Zero-copy scanning for `NALU_TYPE_IDR`, `SPS`, and `SLICE`.
3.  **Frame Type Detection**: Mapping NALUs to I, P, or B frames.
4.  **GOP Model Builder**: Aggregates frames into a stateful GOP structure.

---

## 4. Operational Diagnosis Examples

### 4.1 Stable Broadcast Stream
*   **Input**: `I B B P B B P B B P I B B P`
*   **Analysis**: GOP Length: 10, B-Frame Depth: 2. Status: **NOMINAL**.

### 4.2 Encoder Buffer Stress
*   **Input**: `I B B P B B B B P P P`
*   **Analysis**: Sudden B-frame burst. Status: **WARNING**.
*   **Impact**: Potential decoder buffer overflow or high playback latency.

### 4.3 Switching Issue
*   **Observation**: IDR Interval expected 2s, observed 6s.
*   **Impact**: Slow channel zapping and DVR seek failures.
