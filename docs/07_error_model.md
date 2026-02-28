# TsAnalyzer Error Model
## Phase 1 — Error Detection & Propagation Physics

---

## 1. Model Objective

Transport Stream errors are not isolated events; they propagate through temporal reconstruction and buffer simulation. The TsAnalyzer Error Model defines how a fault in one layer affects the validity of downstream measurements.
**Error → Impact → Propagation → Measurement Validity.**

---

## 2. Error Classification Hierarchy

Errors are classified by their impact on the underlying measurement physics:

| Level | Classification | Impact on Metrology |
| :--- | :--- | :--- |
| **Level 0** | **Anomaly** | Local issue (e.g., PSI delay). Measurement remains **VALID**. |
| **Level 1** | **Structural** | Stream integrity lost (e.g., CC error). State becomes **DEGRADED**. |
| **Level 2** | **Temporal** | Timebase contaminated (e.g., PCR jitter). Timing metrics are **UNSTABLE**. |
| **Level 3** | **Physical Break** | Timeline destroyed (e.g., RX overflow). Measurement is **INVALID**. |

---

## 3. Error Propagation Graph

TsAnalyzer models dependency flow. An upstream failure monotonically contaminates dependent layers:
**Packet Integrity → PCR Validity → STC Reconstruction → AU Scheduling → VBV Evolution → RST Prediction.**

### 3.1 Causal Impact Rules
- **CC Error**: Leads to PES truncation and AU boundary ambiguity. VBV simulation MUST be flagged as **DEGRADED**.
- **PCR Error**: Small errors amplify buffer deviation. PCR invalidity MUST stop all temporal analytics.
- **RST Prediction**: Requires causal certainty. If VBV state is uncertain, engine MUST emit **RST_CONFIDENCE = INVALID**.

---

## 4. Measurement Validity Flags

Each reported metric carries metadata defining its physical reliability:
- **VALID**: Fully trusted measurement based on continuous data.
- **DEGRADED**: Results derived from discontinuous or partial data (e.g., after a CC error).
- **INVALID**: Physically unreliable data (e.g., after a physical timeline break).

---

## 5. Operational Invariants (Normative)

1.  **No Silent Recovery**: The engine SHALL NOT conceal packet loss or resynchronize silently.
2.  **No Interpolation**: Missing timestamps or data segments SHALL NOT be guessed or estimated.
3.  **Traceability**: Every alarm MUST include the absolute byte offset and VSTC timestamp of the triggering fault.
4.  **Integrity Principle**: TsAnalyzer prefers a **Known Invalid Result** over a **Possibly Incorrect Result**.

---

## 6. Discontinuity & Epoch Management

Explicit discontinuity indicators (e.g., af_discontinuity_flag) trigger an **Analytical Epoch Reset**:
- Current measurement context is closed.
- A fresh STC reference and buffer state are initialized.
- This transition is logged as a planned reset, not a failure.
