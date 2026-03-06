# Statistical Multiplexing (StatMux) Reverse Engineering

The StatMux Detection Engine is the most advanced analytical feature of TsAnalyzer Pro. While traditional analyzers only measure aggregate bitrate, TsAnalyzer reverse-engineers the upstream multiplexer's strategy by observing high-frequency timing and bandwidth distribution signals.

---

## 1. Observable Signals

Although the multiplexer is an upstream entity, its internal logic is revealed through:
*   **PID Bitrate Time-Series**: Per-program bandwidth allocation.
*   **PES Timing**: Frame-level cadence shifts.
*   **PCR Clock**: Master timing reference.
*   **T-STD Occupancy**: Decoder buffer fullness response.

---

## 2. Multiplexing Cycle Detection

StatMux typically operates in logic cycles ranging from **100ms to 500ms**. The engine detects this periodicity using **Autocorrelation** of the bitrate time series $B(t)$.
$$R(\tau) = \sum B(t) B(t+\tau)$$
The peaks of $R(\tau)$ reveal the multiplexer's exact scheduling cycle time.

---

## 3. Bitrate Competition Model

Programs within a multiplex compete for a fixed bandwidth pool. TsAnalyzer computes a normalized **Bandwidth Share ($S_i$)**:
$$S_i(t) = \frac{B_i(t)}{\sum B_{all}(t)}$$
This identifies:
*   **Dominant Programs**: Services currently prioritized by the multiplexer.
*   **Suppressed Programs**: Services undergoing aggressive compression to save bandwidth.

---

## 4. Null Packet Displacement (NPD)

NPD measures how aggressively the multiplexer "squeezes" PID 0x1FFF to accommodate service spikes.
*   **Squeeze Rate**: High correlation between service bitrate increases and Null Packet decrease indicates a healthy StatMux.
*   **Saturation Warning**: If Null Packet density remains below 2% for multiple cycles, the multiplex is flagged as **Saturated**, posing an imminent risk of packet loss or visual artifacts.

---

## 5. Phase Alignment Detection

Sophisticated StatMux controllers stagger I-frames across channels to avoid simultaneous bitrate peaks.
*   **Phase Offset**: $\phi_i = \text{argmax } \text{crosscorr}(B_i, B_{total})$
*   **Clustering**: Programs with similar phases are identified as part of the same multiplex group or encoding pool.

---

## 6. Encoder Stress Detection

Multiplexer pressure directly causes encoder stress, visible through:
*   **GOP Size Volatility**: Sudden changes in frame sequencing.
*   **Quantization Spikes**: Reduced picture quality to meet bitrate caps.
*   **Frame Size Oscillation**: Unstable rate control.

---

## 7. Operational Value

*   **Capacity Planning**: Determine how many additional services can be multiplexed without quality loss.
*   **Stability Monitoring**: Detect multiplexer instability before visible video degradation occurs.
*   **Vendor Auditing**: Quantifying and comparing the efficiency of different StatMux implementations.
