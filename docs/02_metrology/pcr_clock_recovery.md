# PCR Clock Recovery & 3D Metrology Model

This document defines the core analytical engine used to reconstruct the sender's continuous system clock from discrete PCR samples. It serves as the mathematical foundation for 3D jitter decomposition, drift estimation, and buffer safety auditing.

---

## 1. The Timing Reconstruction Problem

In MPEG Transport Streams, the Program Clock Reference (PCR) values appear only periodically (typically every 20ms). To achieve laboratory-grade metrology, the analyzer must reconstruct the **continuous encoder clock function $C(t)$** and correlate it with the **Hardware Arrival Time (HAT)**.

### 1.1 42-bit PCR Reconstruction
To ensure precision, the full 27MHz timeline is reconstructed using 64-bit integer arithmetic:
$$PCR_{total} = (PCR_{base} \times 300) + PCR_{ext}$$

**Implementation Reference**:
```c
uint64_t parse_pcr(uint8_t *af) {
    uint64_t base = ((uint64_t)af[0] << 25) | ((uint64_t)af[1] << 17) |
                    ((uint64_t)af[2] << 9)  | ((uint64_t)af[3] << 1)  |
                    ((uint64_t)af[4] >> 7);
    uint16_t ext  = ((af[4] & 1) << 8) | af[5];
    return base * 300 + ext; // 27 MHz ticks
}
```

### 1.2 Q64.64 Fixed-Point Mandate
All slope and clock calculations MUST use **Q64.64 Fixed-Point Arithmetic** to ensure bit-identical results across architectures and prevent rounding error accumulation over 24h+ runs.

---

## 2. 3D PCR Jitter Decomposition

TsAnalyzer unique value proposition is the decomposition of raw jitter into three orthogonal diagnostic vectors:

1.  **PCR_AC (Accuracy)**:
    *   **Source**: Encoder Multiplexer.
    *   **Meaning**: The deviation of the PCR sample from the piecewise-constant bitrate model. Represents the precision of the encoder's PCR insertion logic.
2.  **PCR_DR (Drift Rate)**:
    *   **Source**: Encoder Clock Crystal.
    *   **Meaning**: The long-term linear frequency deviation of the STC vs. the physical wall-clock.
    *   **Calculation**: $Drift_{ppm} = \frac{\Delta PCR - \Delta HAT}{\Delta HAT} \times 10^6$.
3.  **PCR_OJ (Overall Jitter)**:
    *   **Source**: Transport Network.
    *   **Meaning**: The combined phase jitter introduced by network transit, congestion, and buffering.

---

## 3. Continuous Clock Recovery (The Math)

The engine assumes the encoder clock is linear over a sliding window ($N=128$):
$$PCR(t) = a \times t + b$$

### 3.1 Least Squares Regression
The frequency scaling factor $a$ is solved using:
$$a = \frac{Covariance(HAT, PCR)}{Variance(HAT)}$$

### 3.2 Kahan Compensated Summation
For long-term stability, linear regression accumulators employ the Kahan algorithm to maintain floating-point precision during delta accumulation.

---

## 4. Two-Stage Jitter Filtering

1.  **Stage 1: Median Filter (N=5)**: Strips outliers caused by OS scheduling spikes.
2.  **Stage 2: EMA Filter ($\alpha=0.1$)**: Produces a smooth, reliable jitter trend for NOC visualization.

---

## 5. Measurement Precision & Authority

| Timestamp Source | Resolution | Determinism |
| :--- | :--- | :--- |
| **Software Clock** | ~1 µs | Low |
| **Kernel Timestamp**| ~100 ns | Medium |
| **NIC Hardware (HAT)**| **~10 ns** | **High (Instrument Grade)** |

---

## 6. Operational Diagnostics

### 6.1 Discontinuity Epochs
If $| \Delta PCR - \Delta HAT | > 100\text{ms}$, the engine triggers an **Analytical Epoch Reset**, clearing the regression model to prevent contaminated averages.

### 6.2 Remaining Safe Time (RST)
By correlating the reconstructed clock frequency ($a$) with the T-STD buffer fill rate, the engine predicts the **seconds remaining** before imminent underflow/overflow.
