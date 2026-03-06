# PRD: Commercial-Grade TsAnalyzer

## 1. Product Mission
TsAnalyzer is a **Broadcast-Grade Deterministic Monitoring System**. It provides absolute metrology, stateful alarm lifecycle management, and long-term compliance (SLA) auditing.

The operational interface is driven by a **Three-Plane Appliance Architecture** (see `docs/44_grafana_dashboard_spec.md`) and a real-time **Inference Engine** (see `docs/46_inference_engine_implementation.md`).

---

## 2. Alarm Lifecycle Engine (The FSM)

To ensure professional operations, every detected fault must follow a strictly defined **Finite State Machine (FSM)**.

### 2.1 Alarm States
| State | Transition Trigger | Action |
| :--- | :--- | :--- |
| **OPEN** | Condition met (e.g., CC_error detected). | Bind Evidence, create Alarm ID, start T_detect timer. |
| **ACTIVE** | Condition persists > `T_detect` (default 3s). | Trigger Notifications (SNMP/Webhook), increment Error Count. |
| **ACKNOWLEDGED** | Operator manually confirms the alarm. | Silence notifications, mark with `User_ID` and `Timestamp`. |
| **CLEARED** | Condition absent > `T_clear` (default 5s). | Stop Duration timer, calculate total incident length. |

### 2.2 Deterministic Evidence
Every alarm MUST be bound to a bit-exact evidence packet:
- **Source**: The exact Transport Stream PID and Packet Offset.
- **Context**: The `analysis_engine_version` and `metrics_schema_version`.
- **Payload**: The specific values that triggered the fault (e.g., `Expected_CC: 5, Actual_CC: 7`).

---

## 3. State & History Engine

Data is processed and stored in three temporal tiers to balance real-time responsiveness with historical perspective.

### 3.1 Tier 1: Real-time Snapshot (100ms)
- **Content**: Instantaneous P1/P2/P3 status, current bitrate, current PCR jitter.
- **Purpose**: Low-latency NOC dashboards and industrial CLI.

### 3.2 Tier 2: Rolling Window Stats (1s, 10s, 60s)
- **Content**: `avg_jitter`, `p99_latency`, `error_rate_per_minute`.
- **Purpose**: Detecting intermittent "bursty" faults that aren't visible in instant snapshots.

### 3.3 Tier 3: Historical Aggregates (24h, 30-day)
- **Content**: Min/Max/Avg bitrate, **SLA Compliance %** (Availability), Total Error Count.
- **Purpose**: Compliance reporting and long-term link stability auditing.

---

## 4. SLA & Compliance Calculation

TsAnalyzer calculates **Availability** based on the duration of `ACTIVE` alarms:
- **Formula**: `Availability % = (Total_Time - Duration_of_ACTIVE_P1_Alarms) / Total_Time * 100`
- **Thresholds**: Customizable per stream (e.g., 99.9% for Tier 1 Satellite, 99.0% for OTT).

---

## 5. Input Protocol & Mode Support
- **Modes**: File (TS/PCAP), UDP (Unicast/Multicast), SRT (Caller/Listener).
- **Decryption**: AES-128/256 decryption integrated at the Capture Layer.

---

## 6. Forensic & Operational Efficiency

TsAnalyzer is designed to minimize the **MTTR (Mean Time To Repair)** by providing unambiguous evidence and automated forensic capture.

### 6.1 Triggered Forensic Recording (The "Black Box")
When a P1/P2 error is detected or RST falls below the `EMERGENCY` threshold, TsAnalyzer MUST:
- **Automatic Capture**: Trigger a rolling buffer dump of the raw TS data.
- **Pre/Post Buffer**: The capture must include **5 seconds of pre-fault data** and **5 seconds of post-fault data**.
- **Evidence Binding**: The resulting forensic bundle is uniquely linked to the Alarm ID for Plane 3 (Forensic Replay) analysis.

## 7. Deployment & Orchestration
The NOC surface is automatically deployed via `scripts/deploy_dashboard.py`, ensuring that Plane 1 (Wall), Plane 2 (Focus), and Plane 3 (Forensic) are instantiated with correct UIDs, PromQL inference rules, and 4K grid coordinates.
