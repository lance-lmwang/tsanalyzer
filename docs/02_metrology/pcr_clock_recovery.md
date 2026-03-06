# PCR Clock Recovery & Metrology Model

This document defines the core analytical engine used to reconstruct the sender's continuous system clock from discrete PCR samples. This model serves as the mathematical foundation for jitter analysis, drift estimation, and buffer safety auditing.

---

## 1. Problem Definition

In MPEG Transport Streams, the Program Clock Reference (PCR) provides the timing reference for decoders to synchronize video/audio playback and manage buffers. However, PCR values are sparse:
*   **Typical Interval**: 20 ms.
*   **Maximum Allowed**: 40 ms.

The analyzer must reconstruct the **continuous encoder clock function $C(t)$** from these discrete samples to detect micro-instabilities.

---

## 2. PCR Representation & Sender Time

PCR consists of a 33-bit Base (90kHz) and a 9-bit Extension (27MHz).
*   **Total PCR Value**: $PCR_{total} = (PCR_{base} \times 300) + PCR_{ext}$
*   **Sender Time**: $T_{sender} = \frac{PCR_{total}}{27,000,000}$ (seconds)

---

## 3. Dual-Clock Correlation Model

When a PCR packet arrives, the analyzer records a tuple: $(T_{arrival,i}, PCR_i)$.
We track two distinct timelines:
1.  **Encoder Clock (PCR)**: Reflects the multiplexer's internal intent.
2.  **Network Clock (Arrival)**: Reflects physical delivery as measured by the Hardware Timestamp (HAT).

### 3.1 Instantaneous Jitter
The deviation between expected and actual arrival deltas:
$$\Delta T_{sender} = \frac{PCR_i - PCR_{i-1}}{27,000,000}$$
$$\Delta T_{arrival} = T_{arrival,i} - T_{arrival,i-1}$$
$$J_i = \Delta T_{arrival} - \Delta T_{sender}$$

---

## 4. Continuous Clock Reconstruction (Linear Regression)

To filter out transport noise, the engine assumes the encoder clock is approximately linear over short windows:
$$PCR(t) = a \times t + b$$
Where:
*   **$a$**: Clock frequency scaling (Drift).
*   **$b$**: Phase offset.

### 4.1 Least Squares Estimation
Given a window of $N$ samples (default $N=128$), the engine solves for $a$:
$$a = \frac{Covariance(T_{arrival}, PCR)}{Variance(T_{arrival})}$$
This provides a highly stable estimate of the true encoder frequency, immune to individual packet jitter.

---

## 5. Drift Estimation (ppm)

Encoder clocks are rarely perfect. Long-term drift causes buffer overflows or AV desync.
$$Drift_{ppm} = \frac{a_{estimated} - 27,000,000}{27,000,000} \times 10^6$$

| Drift (ppm) | Meaning | Status |
| :--- | :--- | :--- |
| **± 5 ppm** | Excellent master clock | **NOMINAL** |
| **± 20 ppm** | Normal encoder | **NOMINAL** |
| **> 50 ppm** | Unstable clock | **WARNING** |

---

## 6. Two-Stage Jitter Filtering

To ensure measurement precision, raw jitter is processed through a robust filtering pipeline:

1.  **Stage 1: Median Filter**: A window of 5 samples removes statistical outliers caused by OS scheduling spikes or NIC buffer delays.
2.  **Stage 2: Exponential Moving Average (EMA)**:
    $$J_{filtered,i} = \alpha \times J_i + (1 - \alpha) \times J_{filtered,i-1}$$
    *Default $\alpha = 0.1$* provides a smooth, reliable jitter trend.

---

## 7. Handling Discontinuities

PCR may jump due to stream switching or encoder resets.
*   **Detection Rule**: $| \Delta PCR - \Delta T_{arrival} | > 100\text{ms}$.
*   **Action**: Immediately reset the regression model and start a new **Analytical Epoch**.

---

## 8. Measurement Precision & Hardware Authority

The accuracy of jitter and drift metrology is fundamentally limited by the resolution of the arrival timestamp source.

| Timestamp Source | Resolution | Determinism Level |
| :--- | :--- | :--- |
| **Software Clock** | ~1 µs | **LOW**: Heavily affected by OS scheduling. |
| **Kernel Timestamp**| ~100 ns | **MEDIUM**: Affected by IRQ jitter. |
| **NIC Hardware (HAT)**| **~10 ns** | **HIGH**: Bit-exact and deterministic. |

TsAnalyzer integrates hardware-assisted timestamping (HAT) to achieve the nanosecond-level precision required for professional broadcast auditing.

---

## 9. Derived Industrial Metrics

Once the clock is reconstructed, the engine computes:
*   **PCR Jitter**: Peak-to-peak deviation ($max |J_i|$).
*   **PCR Accuracy**: The difference between the intended PCR value and the reconstructed linear baseline.
*   **Remaining Safe Time (RST)**: Predicting the time until the T-STD buffer reaches underflow/overflow based on current drift and arrival rate.
