# Standard Comparison Baseline: Validation Protocol

This document defines the methodology used to ensure TsAnalyzer Pro’s metrology algorithms are functionally aligned with industry-standard hardware meters (e.g., Tektronix Sentry, Rohde & Schwarz VM6000).

---

## 1. Compliance Alignment

TsAnalyzer Pro is built on the principle of **Mathematical Parity**. Instead of black-box heuristics, our engines are direct C-language implementations of international standards:

| Engine Component | Reference Standard | Alignment Method |
| :--- | :--- | :--- |
| **TR 101 290 Engine** | ETSI TR 101 290 v1.4.1 | Strict P1/P2/P3 state machine transitions. |
| **PCR Jitter (AC/DR/OJ)** | ISO/IEC 13818-1 Annex D | Software PLL with 27MHz Virtual System Clock (VSTC). |
| **T-STD Buffer Model** | ISO/IEC 13818-1 Annex D | 3-stage (TB/MB/EB) simulation with deterministic removal. |
| **SCTE-35 Audit** | ANSI/SCTE 35 2022 | Bit-accurate section parsing and splice-time verification. |

---

## 2. Validation Methodology (The "Golden Master" Test)

To verify TsAnalyzer's accuracy without physical hardware access, we utilize the **Golden Master Regression**:

1.  **Input**: A 1GB standard test stream (e.g., EBU Test Set or Tektronix generated capture).
2.  **Oracle**: The known metrology values (Jitter, Bitrate, CC count) provided by the stream's documentation.
3.  **Test**: TsAnalyzer processes the stream in `--mode replay` using Hardware Arrival Timestamps (HAT) preserved in the PCAP.
4.  **Acceptance**: The JSON output must match the Oracle values within the following tolerances:
    *   **Bitrate**: ± 0.001%.
    *   **PCR Jitter**: ± 500ns (limited only by timestamp resolution).
    *   **Buffer Fill**: ± 1 byte.

---

## 3. Why Software Can Surpass Hardware

While traditional hardware meters utilize specialized FPGAs, TsAnalyzer’s software-defined approach offers distinct advantages in **Reproducibility**:

*   **Deterministic Replay**: Given the same PCAP, TsAnalyzer will produce the **exact same MD5 hash** for its analysis report every time. Physical hardware often introduces analog noise or interrupt-based jitter in its internal timing.
*   **Nanosecond Anchoring**: By utilizing `SO_TIMESTAMPING` at the NIC driver level, TsAnalyzer bypasses OS scheduling jitter, achieving timing accuracy previously only possible in dedicated ASICs.

---

## 4. User Verification Guide

Enterprise customers can verify TsAnalyzer's alignment by running our regression suite:
```bash
./scripts/verify_determinism.sh ./sample/golden_master.ts
```
This script confirms that the current engine version adheres to the baseline metrology contract.
