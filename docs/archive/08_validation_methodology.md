# TsAnalyzer Validation Methodology
## Phase 1 — Measurement Verification Framework

---

## 1. Objective

The purpose of validation is to demonstrate that TsAnalyzer produces measurements that are accurate, repeatable, and independently verifiable. This framework establishes TsAnalyzer as a **Software Measurement Instrument**, moving beyond mere analytical software.

---

## 2. Validation Philosophy

TsAnalyzer validation follows the principle of **Controlled Causality**:
**Measurement Claim → Independent Reference → Controlled Experiment → Repeatable Result.**
A metric is considered valid only if its accuracy has been experimentally confirmed against a recognized ground truth.

---

## 3. Validation Domains

| Domain | Objective | Method |
| :--- | :--- | :--- |
| **Deterministic** | Reproducibility | MD5-identical JSON output across 100 runs. |
| **Temporal** | Timing Accuracy | Comparison against hardware-referenced PTP analyzers. |
| **Mathematical** | Model Correctness | Annex D/I simulation vs. theoretical Golden Streams. |
| **Physical** | Real-world Equivalence | Validation against commercial hardware instruments. |

---

## 4. The Golden Stream Framework

TsAnalyzer maintains a library of **Certified Golden Streams**:
- **Baseline Streams**: Perfect timing and structure for noise-floor calibration.
- **Impaired Streams**: Controlled injection of PCR jitter, CC loss, and VBV overflow.
- **Role**: Any change to the analysis engine MUST pass the Golden Stream regression suite to ensure no metrology regressions occur.

---

## 5. Cross-Analyzer Comparison (Audit)

TsAnalyzer results SHALL be compared against laboratory hardware (e.g., Tektronix MTS4000, Rohde & Schwarz).
- **Disagreement Rule**: Any deviation between TsAnalyzer and the reference must be causally explained (e.g., "Deviation due to hardware timestamp resolution vs. software clock domain").
- **Agreement is insufficient** without understanding the underlying causal match.

---

## 6. Synthetic Fault Injection Matrix

To verify the **[Error Model](./07_error_model.md)**, validation includes artificial impairments:

| Injected Fault | Expected Response | Verification Point |
| :--- | :--- | :--- |
| **Packet Loss** | DEGRADED → INVALID | Verification of discontinuity byte-offset. |
| **PCR Jump** | STC Reset | Measurement of STC re-lock convergence time. |
| **Bitrate Burst** | VBV Rise | Correlation between predicted vs. actual overflow. |

---

## 7. Acceptance Criteria for Instrument-Grade Classification

TsAnalyzer is considered validated only when:
1.  **Deterministic Replay** passes bit-identical checks on diverse hardware.
2.  **PCR Accuracy** is verified within ±10ns of a hardware reference.
3.  **Mathematical Simulation** matches the theoretical Annex D curve within 1 Access Unit.
4.  **24h Stability** shows zero metric drift or resource inflation.

---

## 8. Continuous Validation Principle

Validation is not a phase; it is an invariant. Any modification affecting the timing, parsing, or simulation logic requires full re-validation against the Golden Stream library.
