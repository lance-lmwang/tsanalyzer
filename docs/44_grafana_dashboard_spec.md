# TsAnalyzer Pro NOC Dashboard Visual Spec (Ultimate v4.4 - Resilience Edition)

This specification defines the visual and operational standards for the TsAnalyzer Pro NOC Dashboard. It transforms standard monitoring into an **Operational Memory System**, ensuring that transient failures are captured, persisted, and made forensic-ready for asynchronous operator inspections.

---

## 0. Tier 0: Global Fleet Triage Center (The Command Wall)
Instantaneous identification of failures among thousands of streams.
- **Anomaly Persistence**: If a stream recovers from an error, its triage LED MUST remain AMBER for 5 minutes (LATCHED state) to notify roving operators of a recent incident.

---

## 1. Layout Philosophy: The "Fixed Glass" Rule
- **Zero-Scroll**: Single-page interface optimized for 1920x1080.
- **Proportions**: Tier 1 (12%) / Tier 2 (18%) / Tier 3 (20%) / Tier 4 (32%) / Tier 5 (18%).
- **Forensic Time-Sync**: Clicking any point in history synchronizes ALL Tier 1-5 charts to that precise millisecond.

---

## 2. Tier 1: Appliance Vitality & Signal Lock
- **Master Health Score**: 0-100 aggregate score with **Min-Hold logic** (The lowest score in the last 30 minutes is displayed as a 30% opacity ghost value below the current score).

---

## 3. Tier 2: Hybrid Analytics Matrix (Diagnostic Core)
- **Latching Error Counters**: Error blocks (CC, PAT, PMT) show total error counts over a 60-minute sliding window via `increase()` or `sum_over_time()` functions.

---

## 4. Tier 3: Bitrate & Essence Vitals (Historical Timelines)
Designed to expose spikes and dips that occurred while the operator was away.

### 4.1 24-Hour Bitrate Envelope
- **Implementation**: Uses `max_over_time(bitrate[1m])` and `min_over_time(bitrate[1m])` to render a shaded envelope around the average line.

### 4.2 Content Quality Timelines (With Anomaly Highlighting)
- **FPS Stability**: 
    - **Visualization**: Line chart with fixed 25/50/60 reference lines.
    - **Persistence**: Employs a **"Minimum-Value Trace"** (Red line) that tracks the lowest FPS seen in the window. If FPS dropped to 5 and recovered, the Red Trace remains at the bottom of the dip for visibility.
- **GOP Cadence**: Historical trend of I-frame intervals. Spikes in the trend line indicate encoder stalls or scene-change resets.
- **AV Sync / Lip-Sync**: Dual-polarity PTS offset history (+/- 200ms). Deviations from the 0ms center line are permanently recorded in the timeline.

---

## 5. Tier 4: Predictive Horizon & PID Inventory
- **RST Survival Timeline**: History of the "Remaining Safe Time."
- **PID Time-Series**: 24-hour stacked area chart for component distribution.

---

## 6. Tier 5: Operational Audit Trail (Forensic Log)
- **Point-in-Time Drill-Down**: Clicking a dip in Tier 3 automatically scrolls the log to that specific millisecond.

---

## 7. Visual Standards
- **Palette**: Background `#020617`, Panels `#0F172A`.
- **Primary Accent**: `#38BDF8` (Cyan).
- **Forensic Highlight**: A **Vertical Golden Line** marks the point of interest across all charts.

---

## 8. Persistence Implementation Contract (Grafana Enforcement)
Historical persistence MUST NOT rely on visual illusion alone. All persistence behaviors SHALL be backed by explicit query rules.

### 8.1 Latching State Model (Performance-Optimized)
- **NORMAL → ERROR → LATCHED → NORMAL**
- **Logic**: `max_over_time(stream_error_flag[5m])`.
- **Implementation Mandate**: At 1000+ streams, this MUST be implemented as a **Prometheus Recording Rule** (`record: stream_error_latched_5m`) to avoid dashboard CPU spikes.

### 8.2 Minimum-Hold Metric Contract
- **Tier-1 Ghost Value**: `min_over_time(health_score[30m])`.
- **Rendering**: Current (Solid) vs. Ghost (30% Opacity) using the same semantic color.

### 8.3 Safer Query Patterns (Counter Reset Protection)
To prevent false spikes caused by container/exporter restarts, all `increase()` functions SHALL be protected:
- `clamp_min(increase(error_counter[60m]), 0)`

---

## 9. Grafana Panel Mapping Specification
| Tier | Grafana Panel Type | Mandatory Options |
| :--- | :--- | :--- |
| **Tier 1** | Stat + Bar Gauge | Span Nulls = false |
| **Tier 2** | State Timeline | Shared Crosshair |
| **Tier 3** | Time Series | Connect Nulls = NEVER |
| **Tier 4** | Time Series + Table | Shared Tooltip |
| **Tier 5** | Logs Panel | - |

---

## 10. Historical Envelope Rendering Model
### 10.1 Bitrate Envelope Query
- `max_over_time(bitrate[1m])` (Upper Bound)
- `min_over_time(bitrate[1m])` (Lower Bound)
- **Interpretation**: A "Collapse" indicates a stream outage; a "Narrowing" indicates encoder starvation.

---

## 11. Anomaly Highlight Persistence Engine
Transient anomalies SHALL generate persistent markers via **Loki annotations** or the **Grafana Annotation API**.

### 11.1 Anomaly Debouncing
To prevent "annotation storms" at scale, the injection engine MUST enforce a debounce period:
- Inject annotation ONLY IF `last_annotation_type_timestamp > 120s`.

---

## 12. Operator Cognitive Workflow Model
Optimized for a **30-Minute Patrol Cycle**.
1. **Walk-in**: Scan Tier 0 LEDs.
2. **Amber Detected**: Click stream.
3. **Golden Line Sync**: Synchronize all timelines.
4. **Root Cause**: Tier 3/4 identifies if it was a Bitrate, FPS, or Jitter issue.
5. **Confirmation**: Tier 5 log provides forensic proof.
- **Target Diagnosis Time**: < 20 seconds.

---

## 13. Failure Visibility Guarantees
The system SHALL guarantee visibility for the following failures, even if the duration is **< 1 second**:
- FPS Drop, Bitrate Collapse, PAT/PMT Loss, Buffer Near Empty, AV Sync Spike.

---

## 14. Temporal Drift Protection
To ensure the **Golden Line** remains synchronized across all tiers:
- All queries MUST share a standardized `$__fleet_global_interval` step.
- All panels MUST enforce identical time range alignment.

---

## 15. Predictive Maintenance (Future V5)
### 15.1 Incident Density Heatmap
A grid heatmap displaying `sum_over_time(stream_error_flag[24h])`.
- Purpose: Identify encoders or network segments that are statistically unstable, even if currently "Green."
