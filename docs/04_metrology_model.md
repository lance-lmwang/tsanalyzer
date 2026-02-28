# TsAnalyzer Metrology Model
## Phase 1 — Software-Defined Measurement Instrument

---

## 1. Design Objective

The TsAnalyzer Metrology Model defines how observable Transport Stream properties are transformed into traceable engineering measurements. Unlike traditional monitors that report statistics, TsAnalyzer reports **physical measurements** derived from deterministic simulations.

---

## 2. Measurement Philosophy: Reconstruction vs. Sampling

TsAnalyzer operates as a **Software Oscilloscope**. It does not sample packets to estimate health; it reconstructs the entire temporal and physical state of the delivery path:
**Packets → Temporal Reconstruction → Decoder Simulation → Physical State → Measurement.**

---

## 3. Metrology Domains

TsAnalyzer unifies all analysis into four deterministic domains, all sharing the same **[Virtual STC (VSTC)](./02_timing_model.md)** axis.

### 3.1 Transport Integrity Domain
- **Quantity**: Structural correctness (CC evolution, Table versioning, Scrambling transitions).
- **Principle**: Integrity violations are recorded as **state transitions** at specific VSTC points, not just counters.

### 3.2 Temporal Stability Domain
- **Quantity**: PCR Jitter (AC/DR/OJ) and Clock Drift.
- **Model**: Reconstructs the expected PCR progression against the observed arrival mapping.
- **Rigor**: 100% compliant with **ISO/IEC 13818-1 Annex I**.

### 3.3 Delivery Quality Domain (Network Physics)
- **Quantity**: **Media Delivery Index (RFC 4445)**.
- **DF (Delay Factor)**: Represents network-induced decoder stress, computed over the deterministic arrival window.
- **MLR (Media Loss Rate)**: Derived strictly from missing packets; no interpolation permitted.

### 3.4 Decoder Stability Domain
- **Quantity**: Buffer Fullness and **RST (Remaining Safe Time)**.
- **Predictive Power**: RST transforms analysis from reactive detection to **causal prediction** (time remaining before decoder failure).

---

## 4. Measurement Causality Engine

A core invariant: **Every alarm MUST have a measurable physical cause.**
TsAnalyzer records the full causal path:
*Network Jitter → Arrival Dispersion → VBV Drain Acceleration → RST Collapse → Decoder Underflow.*

---

## 5. Traceability Contract

Each measurement reported by the engine is traceable down to the bit-level:
**Output Metric → Simulation State → Access Unit → TS Packet → Absolute Byte Offset.**

---

## 6. Forbidden Metrology Practices (Normative)

To preserve instrument integrity, the engine MUST NOT:
1.  **Average away jitter**: Never smooth out transient physical violations.
2.  **Hide transient errors**: Every violation must be captured, regardless of duration.
3.  **Heuristic estimation**: Only modeled physics (Annex D/I) is allowed.
4.  **Temporal healing**: Never "fix" or infer missing timestamps.

---

## 7. Instrument Identity

TsAnalyzer behaves as a **Software-Defined Measurement Instrument**. Its output is designed for:
- Laboratory validation and encoder certification.
- Forensic incident analysis and SLA dispute resolution.
- Proactive failure prevention through predictive metrology.
