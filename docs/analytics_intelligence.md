# TsAnalyzer: Analytics Intelligence & Alarm Logic

This document defines how TsAnalyzer transforms raw physical measurements into actionable business intelligence and stateful alarm lifecycles.

---

## 1. The Error Model: Propagation & Impact

Errors in a Transport Stream are not isolated; they contaminate downstream analysis.

### 1.1 Error Classification Hierarchy
| Level | Classification | Impact |
| :--- | :--- | :--- |
| **Level 0** | **Anomaly** | Local issue (e.g., PSI delay). Results remain **VALID**. |
| **Level 1** | **Structural** | Stream integrity lost (e.g., CC error). State becomes **DEGRADED**. |
| **Level 2** | **Temporal** | Timebase contaminated (e.g., PCR jitter). Timing is **UNSTABLE**. |
| **Level 3** | **Physical** | Timeline destroyed (e.g., Link Down). Results are **INVALID**. |

### 1.2 Propagation Rules
*   **Contamination**: A Level 1 error (CC gap) monotonically contaminates dependent layers (PES reassembly → VBV simulation → RST prediction).
*   **Traceability**: Every alarm MUST be bound to an **Absolute Byte Offset** and specific physical context (e.g., `Expected_CC: 5, Actual_CC: 7`).

---

## 2. Alarm Lifecycle Engine

To ensure operational clarity, every detected fault follows a strict **Finite State Machine (FSM)**.

### 2.1 State Transitions
`OPEN` (Detected) → `ACTIVE` (Persists > 3s) → `ACKNOWLEDGED` (Manual) → `CLEARED` (Absent > 5s).

*   **T_detect (Debounce)**: Default 3000ms. Prevents alarms from "chattering" due to micro-bursts.
*   **T_clear (Stability)**: Default 5000ms. Ensures the stream is truly stable before closing an incident.

### 2.2 Alarm Policy & Suppression
*   **Dependency Suppression**: If a "Root" alarm (e.g., `Ingress_Link_Down`) is active, all "Child" alarms (`PAT_error`, `CC_error`) are suppressed to prevent alert storms.
*   **Maintenance Windows**: Scheduled windows where alarms are logged but do not trigger notifications.

---

## 3. Causal Analysis (RCA) & Health Models

### 3.1 RCA Scoring Matrix
TsAnalyzer uses a weighted superposition of factors ($C_i$) to attribute faults:
*   **Score_Network**: Driven by MDI-MLR, SRT Retransmit Tax, and MDI-DF spikes.
*   **Score_Encoder**: Driven by PCR Jitter peaks, PTS Slope errors, and T-STD Overflows.
*   **Decision**: If $Score_{net} > 0.6$, fault is attributed to the **NETWORK**.

### 3.2 Predictive Health (RST)
*   **Network Survival ($RST_{net}$)**: Seconds remaining before inline buffers deplete.
*   **Encoder Survival ($RST_{enc}$)**: Seconds remaining before player desync due to clock drift.

---

## 4. Operational Intelligence (Gateway Logic)

The intersection of predictive health (RST) and causal analysis (RCA) dictates the active operational state of the inline gateway.

### 4.1 The Smart Action Matrix
| State | RST Status | RCA Fault Domain | Gateway Proxy Action |
| :--- | :--- | :--- | :--- |
| **Optimal** | $> 15s$ | `0: OK` | **Direct Pass-Through**: Lowest latency relay. |
| **Network Turbulence**| $< 10s$ | `1: NETWORK` | **Paced Relay**: TsPacer engages to smooth egress traffic. |
| **Encoder Degradation**| $< 10s$ | `2: ENCODER` | **Pass-Through + Alert**: Relay continues but marks fault. |
| **Critical Failure** | $< 5s$ | Any | **Forensic Capture**: Dumps 500ms of `.ts` evidence. |

### 4.2 Fail-Safe: The Watchdog Bypass
Because the gateway operates inline, it must never become the bottleneck.
*   **Bypass Condition**: If processing latency $\Delta t_{proc} > 5ms$ for a contiguous window of 100 packets, the gateway panic-switches to **Transparent L4 Bypass**.
*   **Impact**: Analysis is suspended, but the stream routes directly from ingress socket to egress socket via kernel-level forwarding to ensure 0% service disruption.

---

## 5. State & History Engine

Data is managed in three temporal tiers to balance resolution with storage.

1.  **Tier 1: Instantaneous (100ms)**: Raw status flags and instant jitter.
2.  **Tier 2: Rolling Window (1s - 60s)**: Averages, p99 latency, and error rates per minute.
3.  **Tier 3: Historical (1h - 30d)**: Daily averages and **SLA Compliance %**.

### 4.1 SLA & Availability Calculation
Availability is the percentage of time spent without **ACTIVE** Priority 1 faults:
$$Availability \% = \frac{T_{total} - \sum T_{active\_P1\_alarms}}{T_{total}} \times 100$$

*   **Overlapping Alarms**: If multiple P1 alarms are active simultaneously (e.g., PAT_error and SYNC_error), the duration is only counted **once** for the SLA calculation.
*   **Incident Merging**: Rapid repeating errors (e.g., a burst of CC errors) are merged into a single logical **Incident** to reduce alert fatigue.

---

## 6. Failure Domain Inference Engine

The engine formalizes "Banner Truth" for the NOC by translating mathematical factors into concrete classifiers via Prometheus recording rules.

### 6.1 Hierarchical Aggregation
1.  **Level 1: Factor Detectors**: Normalizes metrics (MLR, Jitter, Retransmits) into a 0.0-1.0 probability space.
2.  **Level 2: Domain Superposition**: Weighted sum of factors to calculate `Score_Network` and `Score_Encoder`.
3.  **Level 3: Decision Classifier**: Winner-take-all logic to determine the final banner state.

### 6.2 Decision Matrix
| Value | Inference String | Description |
| :--- | :--- | :--- |
| **0** | `SIGNAL OPTIMAL` | All metrics within nominal bounds. |
| **1** | `NETWORK IMPAIRMENT` | Network score > 0.6; Encoder score < 0.2. |
| **2** | `ENCODER INSTABILITY`| Encoder score > 0.6; Network score < 0.2. |
| **3** | `MULTI-CAUSAL CRITICAL`| Both scores > 0.4. Critical state. |

---

## 7. KPI Aggregation (Business Intelligence)

Abstracts metrology into four high-level indicators for NOC triage:
*   **Risk Level**: CRITICAL, HIGH, or NORMAL based on active alarms and RST.
*   **Compliance Grade**: Grade A (99.99%) to Grade F (<99.0%).
*   **Stability Index**: A 0-100 score quantifying link "turbulence" (Jitter/RTT variance).
*   **Encoder Integrity Score**: Isolates source health from transport network health.
