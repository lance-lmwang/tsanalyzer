# TsAnalyzer Operational Modes
## Phase 1 — Certified Execution Environments

---

## 1. Objective

TsAnalyzer supports multiple operational modes built upon the same deterministic engine core. Each mode defines execution constraints and timing authority to determine the **Measurement Authority** of the results.

---

## 2. Operational Mode Overview

| Mode | Purpose | Measurement Authority |
| :--- | :--- | :--- |
| **Live Capture** | Real-time monitoring | **Operational** (Actionable) |
| **Deterministic Replay**| Laboratory analysis | **Reproducible** (Traceable) |
| **Forensic** | Incident investigation | **Evidential** (Auditable) |
| **Certification** | Compliance validation | **Instrument-grade** (Normative) |

---

## 3. Mode Specifications

### 3.1 Live Capture Mode
- **Purpose**: Continuous observation of live broadcast streams.
- **Authority**: Results are suitable for NOC alerts and operational triage.
- **Constraint**: System scheduling jitter is tolerated; results are considered "Operational."

### 3.2 Deterministic Replay Mode
- **Purpose**: Bit-identical offline analysis of recorded TS/PCAP data.
- **Guarantee**: **Input + Engine + Config = Bit-identical Output.**
- **Constraint**: Wall-clock is removed; only recorded hardware timestamps are used.

### 3.3 Forensic Mode
- **Purpose**: Post-incident causal reconstruction for SLA disputes.
- **Requirement**: Mandatory immutable input dataset with integrity hash binding.
- **Output**: Full error trace emission with **Causal Attribution**; no analytical smoothing allowed.

### 3.4 Certification Mode
- **Purpose**: Formal encoder certification and vendor acceptance testing.
- **Mandatory Environment**:
    - Hardware timestamping enabled.
    - Isolated CPU cores (`isolcpus`).
    - Fixed CPU frequency & NUMA locality enforcement.
- **Failure**: Mode entry is aborted if environmental constraints are violated.

---

## 4. Measurement Authority Hierarchy

Operational modes follow a monotonic hierarchy of trust:
**Live Capture → Deterministic Replay → Forensic → Certification**

### 4.1 Automatic Downgrade Principle
If runtime conditions violate the current mode's guarantees (e.g., detected packet loss due to background load in Certification Mode), TsAnalyzer **SHALL automatically downgrade** the measurement authority level and log the violation.

---

## 5. Mode Declaration Requirement (Normative)

Every analytical report emitted by the engine MUST declare:
1.  **Operational Mode**
2.  **Engine Version & Config Hash**
3.  **Timing Domain Authority** (HAT vs. VSTC)

Measurements without a mode declaration are considered invalid for formal auditing.
