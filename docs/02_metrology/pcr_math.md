# 27MHz PCR Math & PLL Analytics

This document formalizes the software clock reconstruction logic used for all temporal metrology.

## 1. High-Precision Software PLL

TsAnalyzer reconstructs a local System Time Clock (STC) from sparse PCR samples using a digital lock-loop.

### 1.1 Interpolation Formula
Between two PCR samples ($P_1, P_2$) arriving at hardware times ($H_1, H_2$):
*   **Calculated Slope**: $M = \frac{P_2 - P_1}{H_2 - H_1}$
*   **Virtual STC at time $t$**: $VSTC(t) = P_1 + M \times (t - H_1)$

### 1.2 Precision Handling
All slope and clock calculations use **Q64.64 Fixed-Point Arithmetic** to ensure:
1.  **Bit-Identical results** across x86 and ARM.
2.  **No accumulation of rounding errors** over long runs (24h+).

---

## 2. 3D PCR Jitter Decomposition

TsAnalyzer is unique in decomposing raw jitter into three diagnostic vectors:

1.  **PCR_AC (Accuracy)**: The deviation of the PCR sample from the piecewise-constant bitrate model. Represents encoder multiplexing precision.
2.  **PCR_DR (Drift Rate)**: The long-term linear slope of STC vs. physical wall-clock. Revels clock crystal aging (measured in ppm).
3.  **PCR_OJ (Overall Jitter)**: The combined phase jitter introduced by the transport network.

---

## 3. Piecewise Constant Bitrate (PCBR)

For offline forensic analysis, the engine utilizes a **PCBR Model**:
*   The bitrate is assumed constant between PCR samples.
*   Packet indices are used to predict arrival times, allowing for bit-accurate jitter measurement even without original hardware timestamps.
