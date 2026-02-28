# Monitoring PRD: TsAnalyzer Pro

## 1. Monitoring Philosophy
TsAnalyzer Pro monitors the **"Predictive Survival Horizon" (RST)** of a stream. The goal is to provide **10 seconds of warning** before any user-perceivable glitch occurs. Broadcast monitoring is an event-priority system, not a smooth averaging system.

### 1.1 Master Health Score Formula (Piecewise Deterministic)
To ensure 100% consistency across backend, API, and frontend implementations, the Health Score MUST be calculated using this exact piecewise logic:

**Base Score:** `Health_base = 100`

**1. P1 Fatal Penalty (Active Sync/PAT/CC/Freeze/MDI-MLR):**
`If P1_Fatal_Active OR MDI_MLR > 0: Health_base = Health_base - 40`

**2. Network RST & Link Penalty (MDI/SRT Context):**
- `RST_Net > 30s` => `Penalty = 0`
- `10s < RST_Net <= 30s` => `Penalty = 10`
- `5s < RST_Net <= 10s` => `Penalty = 25`
- `RST_Net <= 5s` => `Penalty = 40`
- **MDI Jitter Context**: `If MDI_DF > (0.8 * SRT_Latency): Penalty = Penalty + 15` (Indicates buffer exhaustion risk)
`Health_base = Health_base - Penalty`

**3. Encoder RST Penalty:**
- `RST_Enc > 15s` => `Penalty = 0`
- `5s < RST_Enc <= 15s` => `Penalty = 10`
- `RST_Enc <= 5s` => `Penalty = 20`
`Health_base = Health_base - Penalty`

**4. Link Integrity Penalty (SRT Specific):**
- `SRT_Retransmit_Rate > 15%` => `Penalty = 10`
- `SRT_Unrecovered_Loss > 0` => `Penalty = 30` (Redundant with MDI-MLR but ensures protocol-level capture)

**5. Final Health Calculation (The "Lid" Rule):**
`Health = Health_base - penalties`
`If P1_Fatal_Active OR RST_Net <= 5s OR MDI_MLR > 0: Health = min(Health, 60)`

---

## 2. Hierarchical Monitoring Strategy

TsAnalyzer Pro implements a tiered data architecture to optimize for both **Instant Forensics** and **Scalable Observability**.

| Tier | Focus | Key Metrics | Data Format |
| :--- | :--- | :--- | :--- |
| **Kernel/Forensic** | Real-time capturing and historical extrema tracking. | `bitrate_min/max`, `gop_min/max`, detailed error messages. | Internal C Structs / JSON API |
| **Observability/TSDB** | Long-term trend analysis and visual correlation. | Instantaneous values (current bitrate, current gop_n). | Prometheus Exporter |

### 2.1 The "No-Static-Lines" Principle
We avoid exporting long-term historical extrema (like `global_min_bitrate`) to Prometheus to prevent "Static Line Pollution" in TSDB.
- **Rule**: Prometheus only receives instantaneous observations.
- **Analytics**: Historical ranges in Grafana are calculated dynamically using functions like `min_over_time(metric[range])`.
- **CLI Exception**: In CLI mode, since no database is present, the tool extracts `min/max` directly from the Kernel Tier to provide an immediate summary report.

## 3. Metric Layers (The "Five Tiers")

### Tier 0: Global Fleet Triage (Fleet Grid)
- **Goal**: High-density triage of 1000+ streams.
- **Severity Priority (Weighted)**:
  `Severity Weight = Base_Severity + Duration_Weight`
  - **EMERGENCY (Red)**: Base=100. (Signal Loss, Freeze, RST < 5s, or MDI_MLR > 0).
  - **CRITICAL (Orange)**: Base=50. (RST < 10s, Active P1 Errors, or MDI_DF > 80% Buffer).
- **Duration Weight (Priority Amplifier)**:
    - `Duration > 60s` => `+10`
    - `Duration > 180s` => `+25`
- **Sticky Priority Window**: 60 seconds.
- **Tie-Break Priority (Sorting)**: 1. Lowest RST, 2. Longest Severity Duration, 3. Highest P1 CC Count (60s window).
- **Correlation View**: All overlays MUST align on a **Unified Device Timestamp**.

### Tier 1: Gateway Survival (The Four Masters)
- **Trend Arrow Quantization (30s Window)**:
  - **Network/Encoder RST**: [DOWN] if slope < **-1.0 s/s**.
  - **Forwarding SLA**: [DOWN] if slope < **-0.1 %/s**.
  - **Master Health**: [DOWN] if slope < **-1.0 pts/s**.
  - `[UP] (Green)`: If slope > quantization threshold.
  - `[STABLE] (White)`: Stable state.

### Tier 2: Incident Attribution (Causal Tracks)
- **Tracks**: Hardware Bypass, Signal Loss, CC Error Bursts.
- **Constraint**: Vertically aligned to Unified Timebase.

### Tier 3: Diagnostics Matrix (Metrology Diagnostics)
- **Error Aging Rule**: Displayed values MUST represent the **delta over the last 60s sliding window**. Lifetime accumulators are for background logs only.
- **Latching ACK State**: Acknowledged alarms MUST turn **Solid Amber (Orange)** until the error condition clears.

### Tier 4: Audit Trail (Sequential Forensic)
- **FIFO Buffer**: Max 5,000 entries in browser memory.
- **Severity Colors**: Emergency=Red BG, Critical=Orange Text, Notice=Gray.

---

## 3. Forensic Trigger Logic
When any **Tier 1** metric hits the **Emergency** threshold, TsAnalyzer MUST:
1. Trigger a **Forensic Snapshot** (10s before/after).
2. Generate a `forensic_trace.json` mapping all Tier 3 metrics to the fault event.
3. Notify Cloud Orchestrator via signed webhook.

---

## 4. Industrial-Grade Event Logic

### 4.1 Stream Freeze Detection
- **Condition**: TS packets arriving, but PTS across video PIDs stagnant for > 2.0s.
- **Action**: EMERGENCY Alarm.

### 4.2 Alarm Escalation Timer
- If a **CRITICAL** alarm remains active/unacknowledged for **> 120 seconds**, escalate to **EMERGENCY**.

### 4.3 Time Reference & Continuity
- **Priority**: Device Timestamp > PCR-Derived > Server NTP.
- **Discontinuity Protection**: If timestamp delta > 2.0s, insert a **Visual Break Marker** on the timeline.

---

## 5. Engineering Constraints
- **Tier 4 FIFO**: 5,000 entries.
- **Backend Retention**: 7 days (Configurable).

---

## 6. TsAnalyzer Pro - Alarm FSM (Raw Definition v1.0)

### 1. State Enum
NORMAL, DETECTED, ACTIVE, ACKNOWLEDGED, ESCALATED, CLEARED_PENDING, CLEARED, LATCHED.
**Initial State**: NORMAL.

### 2. Global Timing Parameters (Default)
- T_detect_ms = 3000
- T_clear_ms = 5000
- T_blink_window_ms = 30000
- T_count_window_ms = 60000
- T_escalation_P1_ms = 60000
- T_escalation_P0_ms = 30000
- HYSTERESIS_DOWNGRADE_ms = 5000
- BLINK_FREQUENCY = 2Hz
- BLINK_OPACITY = 1.0 <=> 0.7

### 3. Inputs (Events)
error_detected(severity), error_cleared, operator_ack(user_id), escalation_timeout, latching_enabled(true|false), severity_upgrade(new_severity), severity_downgrade(new_severity).

### 4. State Transition Table
- **NORMAL**: on error_detected -> DETECTED.
- **DETECTED**: if error persists >= T_detect_ms -> ACTIVE; if error_cleared -> NORMAL.
- **ACTIVE**: on operator_ack -> ACKNOWLEDGED; on escalation_timeout -> ESCALATED; on error_cleared -> CLEARED_PENDING; on severity_upgrade -> ACTIVE (reset escalation timer); on severity_downgrade -> ACTIVE (apply downgrade only after HYSTERESIS_DOWNGRADE_ms stable).
- **ACKNOWLEDGED**: on error_cleared -> CLEARED_PENDING; on error persists -> remain ACKNOWLEDGED; on severity_upgrade -> ACTIVE (escalation disabled).
- **ESCALATED**: on operator_ack -> ACKNOWLEDGED; on error_cleared -> CLEARED_PENDING.
- **CLEARED_PENDING**: if error returns -> ACTIVE; if stable >= T_clear_ms -> CLEARED.
- **CLEARED**: if latching_enabled = true AND not acknowledged -> LATCHED; else -> NORMAL.
- **LATCHED**: on operator_ack -> NORMAL; on error_detected -> ACTIVE.

### 5. Escalation Logic
Escalation timer starts when entering ACTIVE.
Timer duration: if severity == P0 -> T_escalation_P0_ms; if severity == P1 -> T_escalation_P1_ms; else -> disabled.
Escalation action: state = ESCALATED; trigger notification pipeline.

### 6. Count Logic
Displayed Count: number_of_error_events within T_count_window_ms sliding window.
Count resets automatically via sliding expiration.

### 7. Blink Logic
Blink applies only if state == ACTIVE AND (current_time - active_enter_timestamp) <= T_blink_window_ms.
ESCALATED: force blink (no time limit).
Blink behavior: 2Hz, opacity oscillation 1.0 <=> 0.7, no full black.

### 8. Audit Trail Write Rules
Write entry on: ACTIVE, ESCALATED, operator_ack, CLEARED, MANUAL_CLEAR, severity_upgrade.
Audit schema: { incident_id, stream_id, severity, state, timestamp_ms, duration_ms, acknowledged_by, escalation_level, count_snapshot }.
Incident ID: Generated when entering ACTIVE from DETECTED. Persists until CLEARED.

### 9. Severity Downgrade Protection
Severity downgrade allowed only if new_severity stable for >= HYSTERESIS_DOWNGRADE_ms. Otherwise ignored.

### 10. Fleet-Level Aggregation
Fleet stream severity priority: ESCALATED > ACTIVE > LATCHED > ACKNOWLEDGED > NORMAL.
Fleet sorting key: Highest severity state, Lowest RST, Longest ACTIVE duration, Highest P1 count.

### 11. Time Discontinuity Protection
If timestamp jump > 2000ms: mark discontinuity, do not trigger DETECTED during discontinuity window.

### 12. Reset Conditions
System restart: All alarms resume from last persisted state. If persistence unavailable -> NORMAL.
