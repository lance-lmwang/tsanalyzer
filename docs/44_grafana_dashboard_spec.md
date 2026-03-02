# TsAnalyzer Pro NOC Dashboard Visual Spec (Ultimate v4.8 - Master Control Room Edition)

This specification defines the visual and operational standards for the TsAnalyzer Pro NOC Dashboard. Designed as a **Broadcast-Grade Appliance NOC** with a hard limit of **128 concurrent streams**, it transforms standard monitoring into a single-node **Operational Memory System**. This ensures transient failures are captured, persisted, and made forensic-ready for asynchronous operator inspections across a 4K UHD layout.

---

## 0. Tier 0: Global Fleet Triage Center (The Command Wall)
Optimized for the human visual scanning limit (~128 objects). Provides instantaneous identification of stream health.
- **Fleet Instability Indicator**: A global top-level gauge showing `Streams in LATCHED state (%)`. 
  - *Actionable Metric*: 1 Amber = Investigate, 20 Amber = Systemic Network/Switch Failure.
- **Anomaly Persistence**: If a stream recovers from an error, its triage LED MUST remain AMBER for 5 minutes (LATCHED state) to notify roving operators on their 30-minute patrol cycles.
- **Focus Mode Context Switch**: Clicking a stream LED instantly transitions the dashboard from "Fleet View" to "Stream Deep Dive". This MUST lock the selected `stream_id` globally, temporarily freeze Fleet View refresh noise, and preserve the Golden Line temporal context to prevent operator disorientation.

---

## 1. Layout Philosophy: The "Fixed Glass" Rule
- **Zero-Scroll**: Single-page interface optimized for **3840x2160 (4K UHD)** viewports.
- **Proportions**:
  - **Tier 1 (Infrastructure & Survival)**: 12%
  - **Tier 2 (Hybrid Diagnostic Matrix)**: 18%
  - **Tier 3 (Essence Stability Timelines)**: 20%
  - **Tier 4 (Predictive & PID Analysis)**: 32%
  - **Tier 5 (Forensic Audit Trail)**: 18% (Positioned dynamically: Either bottom row or a dedicated Right-Hand Drawer/Sidebar).
- **Forensic Time-Sync**: Clicking any point in history synchronizes ALL Tier 1-5 charts to that precise millisecond.

---

## 2. Tier 1: Appliance Vitality & Signal Lock
- **Master Health Score**: 0-100 aggregate score with **Min-Hold logic** (The lowest score in the last 30 minutes is displayed as a 30% opacity ghost value below the current score).

---

## 3. Tier 2: Hybrid Analytics Matrix (Diagnostic Core)
- **Latching Error Counters**: Error blocks (CC, PAT, PMT) show total error counts over a 60-minute sliding window via PromQL.

---

## 4. Tier 3: Bitrate & Essence Vitals (Historical Timelines)
Designed to expose spikes and dips that occurred while the operator was away.

### 4.1 24-Hour Bitrate Envelope
- **Implementation**: Uses `max_over_time(bitrate[$__interval])` and `min_over_time(bitrate[$__interval])` to render a shaded envelope around the average line.

### 4.2 Content Quality Timelines (With Anomaly Highlighting)
- **FPS Stability**: Line chart with fixed 25/50/60 reference lines. Employs a **"Minimum-Value Trace"** (Red line) tracking the lowest FPS in the window via `min_over_time(fps[$__interval])`.
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

## 7. Visual Standards & Browser Guardrails
To prevent Browser Main Thread exhaustion on 4K NOC walls (caused by `mousemove` triggering 20+ panel redraws):
- **Golden Line Throttle**: Crosshair updates MUST be throttled to $\le 20\text{Hz}$ via Grafana settings/plugins.
- **Palette**: Background `#020617`, Panels `#0F172A`.
- **Primary Accent**: `#38BDF8` (Cyan).

---

## 8. Persistence Implementation Contract (Grafana Enforcement)
Because the appliance is capped at 128 streams, heavy queries are executed online via Prometheus.

### 8.1 Latching State Model (Online Evaluation)
- **NORMAL → ERROR → LATCHED → NORMAL**
- **Logic**: `max_over_time(stream_error_flag[5m])`.

### 8.2 Minimum-Hold Metric Contract
- **Tier-1 Ghost Value**: `min_over_time(health_score[30m])`.
- **Rendering**: Current (Solid) vs. Ghost (30% Opacity) using the same semantic color.

### 8.3 Precision Counter Queries
- **Mandate**: Use `clamp_min(increase(error_counter[60m]), 0)` or natively handle it via Grafana panel **Decimals = 0** to hide extrapolation artifacts.

---

## 9. Grafana Panel Rendering Contract
To prevent browser GPU/DOM exhaustion when fetching high-density 1s scrape data:
- **Max Data Points**: MUST be strictly capped at **≤ 3000 per panel**.
- **Min Interval**: MUST be configured to **10s–30s** for long-horizon views.
- **Query Downsampling**: Long-range queries (>6h) MUST rely on `$__interval` downsampling, explicitly overriding raw high-frequency scrape resolutions to protect memory.
- **Connect Nulls**: NEVER (Missing telemetry is an anomaly).
- **Span Nulls**: false.

---

## 10. Historical Envelope Rendering Model
### 10.1 Bitrate Envelope Query
- `max_over_time(bitrate[$__interval])` (Upper Bound)
- `min_over_time(bitrate[$__interval])` (Lower Bound)

---

## 11. Anomaly Highlight Persistence Engine
Transient anomalies SHALL generate persistent markers via **Loki annotations** or the **Grafana Annotation API**.

### 11.1 Context-Aware Anomaly Debouncing
To prevent "annotation storms" crashing the Grafana API during synchronized events (e.g., core switch reboot hitting all 128 streams):
- **Per-Stream Guard**: Inject annotation ONLY IF `(now() - last_annotation_time{stream_id, error_type}) > 120s`.
- **Global Guard Limit**: MUST enforce a hard cap of **≤ 50 annotations / 10s** across the entire appliance.

---

## 12. Operator Cognitive Workflow Model
Optimized for a **30-Minute Patrol Cycle**.
1. **Walk-in**: Scan Tier 0 Fleet Indicator & LEDs (Amber Latch).
2. **Focus Dive**: Click stream to lock operational context.
3. **Golden Line Sync**: Synchronize all timelines to the fault origin.
4. **Root Cause**: Tier 3/4 identifies if it was a Bitrate, FPS, or Jitter issue.
5. **Confirmation**: Tier 5 log provides forensic proof.
- **Target Diagnosis Time**: < 20 seconds.

---

## 13. High-Frequency Metrology (<1s Visibility)
The 128-stream limit allows for extreme temporal accuracy:
- **Scrape Interval**: `1s`.
- **Evaluation Interval**: `1s`.
- This guarantees micro-anomalies (FPS drops, bitrate collapses) are captured by Prometheus.

---

## 14. Temporal Authority & Drift Protection
### 14.1 System Time Authority Contract
To prevent NOC trust collapse ("Logs say 12:01:03, chart says 12:01:04"), a single authoritative operational time domain MUST be enforced:
- **The Source of Truth**: The **Prometheus Scrape Timestamp** is the sole authority for all cross-panel rendering.
- **Normalization**: All encoder-derived timestamps (PCR/PTS) and internal appliance buffers MUST be normalized relative to the scrape time before metric emission.

### 14.2 Absolute Golden Line Sync
The dashboard MUST employ **Dashboard Time Override Injection**. Clicking the forensic timeline forces a URL parameter update (`&from=...&to=...`), coercing absolute millisecond synchronization across all backend queries (Prometheus & Loki).
