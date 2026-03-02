# TsAnalyzer Pro NOC Dashboard Visual Spec (Ultimate v5.0 - Hierarchical Fleet Edition)

This specification defines the visual and operational standards for the TsAnalyzer Pro NOC Dashboard. Recognizing that human cognitive limits peak before hardware limits, this design transforms standard monitoring into a **Progressive Observability System**, ensuring deterministic forensic tracing for fleets up to 1024+ streams without overwhelming the operator.

---

## 0. Appliance Scaling Contract (The Human Limit)
The dashboard SHALL maintain a maximum direct visualization density of **$\le 128$ streams per view** (The Operator Decision Bandwidth Limit). Scaling beyond 128 streams MUST be achieved through **hierarchical fleet aggregation**, rather than indiscriminately increasing visual LED density.

### Backend vs Frontend Scaling Trajectory
| Stream Count | Backend Architecture | Frontend Architecture |
| :--- | :--- | :--- |
| **$\le 128$** | Single Prometheus | Single Wall (128 LEDs) |
| **$256$** | Single Prom + Recording Rules | Level 1 Summary + Drill-Down |
| **$512$** | Dual Shard (Federation) | Hierarchical Topology Mapping |
| **$1024+$** | Thanos/Mimir Global View | Macro Heatmaps & Grouping |

*Product Positioning*: "Supports monitoring fleets up to 1024 streams, powered by 128-stream operational visibility windows."

---

## 1. Progressive Observability (The 3-Level NOC Hierarchy)
To prevent visual failure during massive events, the dashboard implements a three-tier cognitive funnel.

### Level 1: Fleet Summary (Blast Radius Visualization)
- **Top-Level Navigation**: Streams are aggregated by Site, Encoder Group, or Network Segment (e.g., `Site A (64)`, `Site B (128)`).
- **Fleet Instability Indicator**: A global gauge showing `Streams in LATCHED state (%)` per group. 
  - *Actionable Metric*: 1 Amber = Investigate, 20 Amber = Systemic Network/Switch Failure.

### Level 2: Group Wall (The Active Viewport)
- Activated by clicking a Group from Level 1.
- **Triage Matrix**: A high-density grid of micro-LEDs capping at 128 streams.
- **Anomaly Persistence**: Triage LEDs MUST remain AMBER for 5 minutes (LATCHED state) after recovery to aid roving operators on 30-minute patrols.

### Level 3: Stream Focus Mode (Deep Dive)
- Activated by clicking a specific LED in Level 2.
- **Operational Context Switch**: Instantly transitions the dashboard to single-stream diagnostics. This MUST lock the selected `stream_id` globally, freeze Level 2 refresh noise, and preserve the Golden Line temporal context to prevent operator disorientation.

---

## 2. Layout Philosophy: The "Fixed Glass" Rule
- **Zero-Scroll**: Single-page interface optimized for **3840x2160 (4K UHD)** viewports.
- **Proportions**:
  - **Tier 1 (Infrastructure & Survival)**: 12%
  - **Tier 2 (Hybrid Diagnostic Matrix)**: 18%
  - **Tier 3 (Essence Stability Timelines)**: 20%
  - **Tier 4 (Predictive & PID Analysis)**: 32%
  - **Tier 5 (Forensic Audit Trail)**: 18% (Positioned dynamically based on aspect ratio).
- **Forensic Time-Sync**: Clicking any point in history synchronizes ALL Tier 1-5 charts to that precise millisecond.

---

## 3. Tier 1: Appliance Vitality & Signal Lock
- **Master Health Score**: 0-100 aggregate score with **Min-Hold logic** (The lowest score in the last 30 minutes is displayed as a 30% opacity ghost value below the current score).

---

## 4. Tier 2: Hybrid Analytics Matrix (Diagnostic Core)
- **Latching Error Counters**: Error blocks (CC, PAT, PMT) show total error counts over a 60-minute sliding window via PromQL.

---

## 5. Tier 3: Bitrate & Essence Vitals (Historical Timelines)
Designed to expose spikes and dips that occurred while the operator was away.

### 5.1 24-Hour Bitrate Envelope
- **Implementation**: Uses `max_over_time(bitrate[$__interval])` and `min_over_time(bitrate[$__interval])` to render a shaded envelope around the average line.

### 5.2 Content Quality Timelines (With Anomaly Highlighting)
- **FPS Stability**: Line chart with fixed 25/50/60 reference lines. Employs a **"Minimum-Value Trace"** (Red line) tracking the lowest FPS in the window via `min_over_time(fps[$__interval])`.
- **GOP Cadence**: Historical trend of I-frame intervals. Spikes indicate encoder stalls or scene-change resets.
- **AV Sync / Lip-Sync**: Dual-polarity PTS offset history (+/- 200ms).

---

## 6. Tier 4: Predictive Horizon & PID Inventory
- **RST Survival Timeline**: History of the "Remaining Safe Time."
- **PID Time-Series**: 24-hour stacked area chart for component distribution.

---

## 7. Tier 5: Operational Audit Trail (Forensic Log)
- **Point-in-Time Drill-Down**: Clicking a dip in Tier 3 automatically scrolls the log to that specific millisecond.

---

## 8. Visual Standards & Browser Guardrails
To prevent Browser Main Thread exhaustion on 4K NOC walls (caused by `mousemove` triggering 20+ panel redraws):
- **Golden Line Throttle**: Crosshair updates MUST be throttled to $\le 20\text{Hz}$ via Grafana settings/plugins.
- **Palette**: Background `#020617`, Panels `#0F172A`.
- **Primary Accent**: `#38BDF8` (Cyan).

---

## 9. Persistence Implementation Contract (Grafana Enforcement)
Heavy queries are executed online via Prometheus.

### 9.1 Latching State Model (Online Evaluation)
- **NORMAL → ERROR → LATCHED → NORMAL**
- **Logic**: `max_over_time(stream_error_flag[5m])`.

### 9.2 Minimum-Hold Metric Contract
- **Tier-1 Ghost Value**: `min_over_time(health_score[30m])`.
- **Rendering**: Current (Solid) vs. Ghost (30% Opacity) using the same semantic color.

### 9.3 Precision Counter Queries
- **Mandate**: Use `clamp_min(increase(error_counter[60m]), 0)` or natively handle it via Grafana panel **Decimals = 0** to hide extrapolation artifacts.

---

## 10. Grafana Panel Rendering Contract
To prevent browser GPU/DOM exhaustion when fetching high-density 1s scrape data:
- **Max Data Points**: MUST be strictly capped at **≤ 3000 per panel**.
- **Min Interval**: MUST be configured to **10s–30s** for long-horizon views.
- **Query Downsampling**: Long-range queries (>6h) MUST rely on `$__interval` downsampling, explicitly overriding raw high-frequency scrape resolutions to protect memory.
- **Connect Nulls**: NEVER (Missing telemetry is an anomaly).
- **Span Nulls**: false.

---

## 11. Historical Envelope Rendering Model
### 11.1 Bitrate Envelope Query
- `max_over_time(bitrate[$__interval])` (Upper Bound)
- `min_over_time(bitrate[$__interval])` (Lower Bound)

---

## 12. Anomaly Highlight Persistence Engine
Transient anomalies SHALL generate persistent markers via **Loki annotations** or the **Grafana Annotation API**.

### 12.1 Context-Aware Anomaly Debouncing
To prevent "annotation storms" crashing the Grafana API during synchronized events (e.g., core switch reboot hitting all streams):
- **Per-Stream Guard**: Inject annotation ONLY IF `(now() - last_annotation_time{stream_id, error_type}) > 120s`.
- **Global Guard Limit**: MUST enforce a hard cap of **≤ 50 annotations / 10s** across the entire appliance.

---

## 13. High-Frequency Metrology (<1s Visibility)
The 128-stream viewport limit allows for extreme temporal accuracy:
- **Scrape Interval**: `1s`.
- **Evaluation Interval**: `1s`.
- This guarantees micro-anomalies (FPS drops, bitrate collapses) are captured by Prometheus without aggregation hiding them.

---

## 14. Temporal Authority & Drift Protection
### 14.1 System Time Authority Contract
To prevent NOC trust collapse ("Logs say 12:01:03, chart says 12:01:04"), a single authoritative operational time domain MUST be enforced:
- **The Source of Truth**: The **Prometheus Scrape Timestamp** is the sole authority for all cross-panel rendering.
- **Normalization**: All encoder-derived timestamps (PCR/PTS) and internal appliance buffers MUST be normalized relative to the scrape time before metric emission.

### 14.2 Absolute Golden Line Sync
The dashboard MUST employ **Dashboard Time Override Injection**. Clicking the forensic timeline forces a URL parameter update (`&from=...&to=...`), coercing absolute millisecond synchronization across all backend queries (Prometheus & Loki).
