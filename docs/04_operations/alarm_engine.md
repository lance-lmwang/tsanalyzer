# The Stateful Alarm & Incident Engine

TsAnalyzer employs a sophisticated event management system designed to eliminate alert fatigue and provide actionable operational data.

## 1. Alarm vs. Incident: The Grouping Model

To prevent thousands of redundant alerts, the engine distinguishes between raw signals and logical events.

*   **Alarm (Signal)**: An instantaneous violation of a threshold (e.g., "TS Packet #5423 has an invalid CC").
*   **Incident (Event)**: A stateful grouping of related Alarms (e.g., "CC Error Incident on PID 0x100 starting at T1, lasting 3.2s, containing 1000 errors").

---

## 2. The Finite State Machine (FSM)

Every Incident follows a strictly managed lifecycle.

### 2.1 State Transitions
```text
  [OPEN] ----(T_persist)----> [ACTIVE] ----(Manual)----> [ACKNOWLEDGED]
    |                            |                            |
    +--------(T_absent)----------+------------+---------------+
                                              |
                                          [CLEARED]
```

### 2.2 The SUPPRESSED State
An incident enters the **SUPPRESSED** state if its "Root Cause" is already covered by a higher-level fault.
*   **Example**: If `Ingress_Link_Down` is ACTIVE, then a simultaneous `PAT_error` is marked as SUPPRESSED.
*   **Visibility**: Suppressed incidents are logged for forensics but do not trigger NOC notifications or contribute to the Compliance Grade penalty.

---

## 3. Incident Merging (Anti-Storm Logic)

To handle intermittent faults ("chattering"), the engine implements **Dampening**:
*   If an incident clears but re-opens within **T_reopen** (default 5s), it is merged back into the previous logical incident rather than creating a new one.
*   This preserves the continuity of the fault for SLA reporting.

---

## 4. Operational Metrology JSON

Incidents are reported via the API with high-fidelity metadata:
```json
{
  "incident_id": "INC-20260306-001",
  "type": "CC_ERROR",
  "status": "ACTIVE",
  "suppressed": false,
  "first_occur": 1772804183000,
  "last_occur": 1772804186200,
  "count": 1024,
  "details": { "pid": 256, "gap_size": 2 }
}
```
