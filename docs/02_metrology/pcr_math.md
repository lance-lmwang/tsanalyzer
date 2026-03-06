# 27MHz PCR Math & PLL Analytics

This document formalizes the software clock reconstruction logic used for all temporal metrology.

## 1. High-Precision Software PLL

TsAnalyzer reconstructs a local System Time Clock (STC) from sparse PCR samples using a digital lock-loop.

### 1.1 PCR Extraction
PCR values are extracted using 64-bit integer arithmetic:
```c
uint64_t parse_pcr(uint8_t *af) {
    uint64_t base = ((uint64_t)af[0] << 25) | ((uint64_t)af[1] << 17) |
                    ((uint64_t)af[2] << 9)  | ((uint64_t)af[3] << 1)  |
                    ((uint64_t)af[4] >> 7);
    uint16_t ext  = ((af[4] & 1) << 8) | af[5];
    return base * 300 + ext; // 27 MHz ticks
}
```

### 1.2 PLL State Update
The PLL filters network jitter to recover the encoder's true frequency:
```c
void pll_update(pcr_pll_t *pll, double arrival, double pcr) {
    double err = arrival - pcr;
    pll->phase += 0.01 * err;    // Phase correction
    pll->freq  += 0.0001 * err;  // Frequency tracking
    pll->jitter = 0.9 * pll->jitter + 0.1 * fabs(err); // Jitter estimation
}
```

### 1.3 Precision Handling
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
