# TsAnalyzer Pro NOC Dashboard Visual Spec

This specification defines the visual, operational, and spatial standards for the TsAnalyzer Pro NOC. It follows a **Three-Plane Appliance Architecture** designed to transition an operator from fleet-wide situational awareness to millisecond-accurate forensic evidence.

---

## Operational Surface Overview (Three-Plane Model)

```text
================================================================================
                     TsAnalyzer Pro — NOC OPERATIONS SURFACE
================================================================================
                  THREE-PLANE APPLIANCE ARCHITECTURE
================================================================================

PLANE 1 — GLOBAL STREAM WALL  (Tier-0 Fleet Awareness)
Legend: 🟢 Stable  🟡 Degraded  🔴 Failure  🟠 Latched  🔵 Drop  ⚫ No Signal

┌──────────────────────────────────────────────────────────────────────────────┐
│ SITE A                                                                        │
│ CH01🟢 CH02🟢 CH03🟢 CH04🟡 CH05🟢 CH06🟢 CH07🟢 CH08🟢                         │
│ CH09🟢 CH10🟢 CH11🟢 CH12🔴 CH13🟠 CH14🟢 CH15🟢 CH16🟢                         │
│ CH17🟢 CH18🟢 CH19🟡 CH20🟢 CH21🟢 CH22🟢 CH23🔵 CH24🟢                         │
└──────────────────────────────────────────────────────────────────────────────┘
Operator Action: CLICK STREAM → Focus View
                                │
                                ▼
PLANE 2 — STREAM FOCUS VIEW  (Tier-1 → Tier-5 Analysis)
┌──────────────────────────────────────────────────────────────────────────────┐
│ TIER 1 — FAILURE DOMAIN COMMAND BAR (LOCKED, HEALTH, DETERMINISM)            │
├──────────────────────────────────────────────────────────────────────────────┤
│ TIER 2 — HYBRID DIAGNOSTIC MATRIX (SRT/NETWORK + TR101-290 P1)               │
├──────────────────────────────────────────────────────────────────────────────┤
│ TIER 3 — ESSENCE & BITRATE ANALYSIS (ENVELOPE, FPS, GOP, AV SYNC)            │
├──────────────────────────────────────────────────────────────────────────────┤
│ TIER 4 — PREDICTIVE HORIZON (RST SURVIVAL + PID INVENTORY)                   │
├──────────────────────────────────────────────────────────────────────────────┤
│ TIER 5 — OPERATIONAL AUDIT TRAIL (CHRONOLOGICAL EVENT LOG)                   │
└──────────────────────────────────────────────────────────────────────────────┘
Operator Action: CHART CLICK → Forensic Jump
                                │
                                ▼
PLANE 3 — FORENSIC REPLAY (Bit-Exact Memory)
┌──────────────────────────────────────────────────────────────────────────────┐
│ TIMELINE: ─────────────●───────────── (FAILURE POINT ALIGNMENT)              │
├──────────────────────────────────────────────────────────────────────────────┤
│ METRIC DRIFT: PCR / CC / SRT / BITRATE                                       │
├──────────────────────────────────────────────────────────────────────────────┤
│ PACKET INSPECTOR: PID / OFFSET / ERROR PAYLOAD                               │
└──────────────────────────────────────────────────────────────────────────────┘
```

---

## Design Philosophy: Grafana → Appliance Evolution

To achieve broadcast-grade reliability, the UI must transition from a "Generic Dashboard" to a "Specialized Measurement Instrument."

```text
================================================================================
                 GRAFANA → APPLIANCE UI EVOLUTION MAP
================================================================================

┌──────────────────────────────┬─────────────────────────────────────────────┐
│        GRAFANA DASHBOARD      │        TsAnalyzer APPLIANCE UI              │
├──────────────────────────────┼─────────────────────────────────────────────┤
│ GLOBAL OVERVIEW              │ GLOBAL STREAM WALL                          │
│ ───────────────              │ ───────────────────                         │
│ CPU % / Memory / Network     │ CH01🟢 CH02🟢 CH03🔴 CH04🟠               │
│ [Many Charts]                │ ► STATE FIELD / FAILURE VISIBILITY          │
│                              │ ► ZERO NUMBER READING                       │
│ Problem: User reads charts   │ Operator SEES failure instantly             │
├──────────────────────────────┼─────────────────────────────────────────────┤
│ STREAM DASHBOARD             │ STREAM FOCUS (ZERO-SCROLL)                  │
│ ────────────────             │ ───────────────────────────                 │
│ Bitrate / Packet Loss / Jitter│ ┌ LOCK │ DOMAIN │ HEALTH │                  │
│ Audio / Video Tables         │ ├─────────────────────────┤                 │
│ Scroll ↓↓↓                   │ │ DIAGNOSTIC MATRIX (NAK/CC/RTT/BUFFER)     │
│                              │ ├─────────────────────────┤                 │
│                              │ │ BITRATE + ESSENCE / FAILURE PREDICTION    │
│                              │ ├─────────────────────────┤                 │
│                              │ │ FORENSIC LOG                              │
│ Problem: Scroll + Context Loss│ Zero Scroll Decision Path                   │
├──────────────────────────────┼─────────────────────────────────────────────┤
│ LOG PANEL                    │ FORENSIC MEMORY                             │
│ ─────────                    │ ───────────────                             │
│ Timestamp / Severity / Msg   │ Timeline ──●── / Packet Evidence            │
│ Logs separated               │ PID / CC exact / Bit-exact replay           │
│                              │ Evidence synchronized                       │
├──────────────────────────────┼─────────────────────────────────────────────┤
│ REFRESH MODEL                │ RENDER ISOLATION                            │
│ ─────────────                │ ─────────────────                           │
│ Whole dashboard refresh      │ Plane Isolation (Wall / Focus / Replay)     │
│ Grafana redraw stall         │ Appliance latency safe                      │
└──────────────────────────────┴─────────────────────────────────────────────┘
```

---

## 0. Three-Plane Appliance Architecture

To ensure UI responsiveness and operational focus, the NOC is divided into three isolated functional planes (Dashboards):

| Plane | UID | Function | Cognitive Role |
| :--- | :--- | :--- | :--- |
| **Plane 1** | `global-wall` | **Tier 0: Global Stream Wall** | Fleet Situation Awareness |
| **Plane 2** | `stream-focus`| **Tier 1–5: Focus View** | Diagnostic & Prediction |
| **Plane 3** | `forensic-replay`| **Forensic Memory** | Bit-Exact Evidence Audit |

---

## 1. Tier 0: Global Stream Wall (Mosaic Packing Architecture)

The Global Stream Wall is the primary entry point. It uses an **Industry Standard Mosaic Packing Algorithm** to maximize visual occupancy while minimizing operator eye-scan latency.

### 1.1 The Mosaic Packing Algorithm (Constant Human Load)
The wall does not use a fixed grid. Instead, it dynamically calculates the optimal $cols \times rows$ layout based on the current `stream_count` and the screen aspect ratio ($3840/2160 = 1.78$).

**Mathematical Model:**
- $N$ = Current Stream Count (Max 128)
- $A$ = Screen Aspect Ratio (1.78)
- $cols = \lceil \sqrt{N \times A} \rceil$
- $rows = \lceil N / cols \rceil$

**Resulting Densities:**
- **N=8 (Large Channel Mode)**: $4 \times 2$ Grid | Tile: $960 \times 1080$ px.
- **N=64 (Standard Density)**: $11 \times 6$ Grid | Tile: $349 \times 360$ px.
- **N=128 (Status Monitoring Mode)**: $16 \times 8$ Grid | Tile: $240 \times 270$ px.

### 1.2 Last-Row Centering (Industrial Alignment)
To maintain visual stability, if the last row is incomplete, it MUST use **Dynamic Center Packing**:
- *Rule*: The last row of tiles is horizontally centered relative to the screen width, matching broadcast mosaic standards.

### 1.3 Operational State Semantics (Appliance Logic)
Tiles display **Operational States**, not just raw alarms.

| Color | Meaning | Description |
| :--- | :--- | :--- |
| 🟢 | **Stable** | Protocol and timing within nominal limits. |
| 🟡 | **Degraded** | Non-critical variance (e.g., minor jitter, low RST). |
| 🔴 | **Active Failure** | Critical P1 error or signal loss currently occurring. |
| 🟠 | **Latched Failure** | **Recovered but Unacknowledged**. Fault occurred recently. |
| 🔵 | **Analysis Dropped** | Internal analyzer overflow (Internal Drop > 0). |
| ⚫ | **No Signal** | No transport packets detected for > 5s. |

### 1.4 The Latched Failure (NOC Memory)
To prevent transient errors from being missed by operators, the wall implements a **Latching State Machine**:
- **Logic**: `NORMAL → ERROR → LATCHED → CLEAR`.
- **Implementation**: Uses `max_over_time(stream_error_flag[5m])`. If an error occurred in the last 5 minutes but is currently cleared, the state is **LATCHED (Orange)**.

### 1.5 Navigation: The Focus Trigger
Each tile is an interactive portal:
- **Action**: Click Stream Tile.
- **Target**: `/d/stream-focus?var-stream=${__value.text}&from=now-15m&to=now`
- **Impact**: Instantly transitions from Plane 1 (Fleet) to Plane 2 (Focus) with localized context.

---

## 2. Plane 2: Focus View (Tier 1–5 Detailed Analysis)

The Focus View is a **Zero-Scroll** interface optimized for **3840x2160 (4K UHD)**. It follows strict spatial constraints to ensure GPU alignment and crisp rendering on video walls.

### 2.1 Appliance Scaling Contract (The Human Limit)
The Focus View SHALL maintain a maximum direct visualization density of **$\le 1$ stream per view** (The Operator Focus Limit). Multiple Focus views must be handled via Plane 1 aggregation.

### 2.2 4K UHD Spatial Layout Grid
- **Resolution**: 3840 × 2160 px
- **Grid Unit**: 8 px
- **Safe Margins**: Top/Bottom: 24px | Left/Right: 32px
- **Usable Canvas**: 3776 × 2112 px

### 2.3 Vertical Tier Allocation
The screen is divided by decision latency, following a natural top-to-bottom eye-scan path.

| Tier | Function | Height | Y-Range | Cognitive Role |
| :--- | :--- | :--- | :--- | :--- |
| **Tier 1** | Failure Domain & Lock | 192px | 24 - 216 | Instant Decision |
| **Tier 2** | Hybrid Diagnostic Matrix | 320px | 216 - 536 | Rapid Diagnosis |
| **Tier 3** | Essence & Bitrate (Primary) | 864px | 536 - 1400 | Pattern Detection |
| **Tier 4** | Predictive & PID Analysis | 448px | 1400 - 1848 | Forecast / Drill-down |
| **Tier 5** | Forensic Audit Log | 288px | 1848 - 2136 | Forensic Memory |

---

## 3. Tier 1: Failure Domain Command Bar (Y: 24-216)
- **Signal Lock LED** (X:32, W:192): 192x192px state indicator (LOCKED #1C8C5E / LOST #C0392B).
- **Failure Domain Banner** (X:240, W:1600): Large-scale text using **Weighted Inference Correlation**.
  - *Logic*: `Network_Prob = (P1.4 * 0.4) + (SRT_NAK * 0.3) + (RTT_Var * 0.3)`.
- **Master Health Score** (X:1840, W:480): Aggregate health with Min-Hold ghosting.
- **Determinism Metrics** (X:2320, W:736): Internal Drops and Worker Overruns.
- **Clock / Focus State** (X:3056, W:768): Displays `🔒 STREAM FOCUS LOCKED` and Operator Session data.

---

## 4. Tier 2: Hybrid Analytics Matrix (Y: 216-536)
Centered diagnostic 2x4 grid (888px total width). Each cell is 420x72px with 24px gaps.

| Slot | Left Group: SRT/Network | Right Group: TS Compliance (TR 101 290 P1) | Operator Implicit Action |
| :--- | :--- | :--- | :--- |
| **Row 1** | Link Capacity (Headroom) | **P 1.1: TS Sync Loss** | Check Physical Link / Signal Presence |
| **Row 2** | NAK Count (Loss Events) | **P 1.3: PAT Error** | Check Muxer / PSI Table Cycle |
| **Row 3** | Buffer Margin (ms) | **P 1.5: PMT Error** | Check Stream Mapping / SI Integrity |
| **Row 4** | RTT Variance (Jitter) | **P 1.4: CC Error** | Correlate with SRT NAKs (Network Loss) |

---

## 5. Tier 3: Essence & Bitrate Analytics (Y: 536-1400)
The Primary Inspection Zone consuming 40% of the screen.

### 5.1 Bitrate Envelope Panel (Width: 2400px)
- **Top**: 24h Bitrate trend (Avg line + Min/Max envelope).
- **Bottom**: Jitter / MDI-DF trend.
- **Volatility Encoding**: Shaded area opacity scales with bitrate variance.

### 5.2 Content Quality Stack (Width: 1376px)
Three vertical sparklines (1376x256px each):
- **FPS Stability**: With 25/50/60 reference lines and **Red Minimum Trace**.
- **GOP Cadence**: Interval trend (ms).
- **AV Sync**: Dual-polarity PTS drift (+/- 200ms).

---

## 6. Tier 4: Predictive Horizon & PID Inventory (Y: 1400-1848)
- **RST Survival Timeline** (W:1800): Includes **Time to Failure Estimate** (e.g., `Buffer Exhaustion ≈ 42s`).
- **PID Inventory & Stacked Area** (W:1976): Shows V/A/Null distribution. Minimum band height $\ge 6px$.

---

## 7. Tier 5: Operational Audit Trail (Y: 1848-2136)
Full-width (3776px) high-density forensic console.
- **Interaction**: Clicking any chart fluctuation above scrolls this log to the exact millisecond instantly.
- **Columns**: Timestamp (320px), Stream ID (240px), Event (800px), Domain (320px), Severity (240px).

---

## 8. Progressive Observability & Context
1. **Level 1 Summary**: Site/Rack topology with **Fleet Instability Indicator** (Latched Streams %).
2. **Level 2 Group Wall**: Failure wave propagation animation originating from `first_error_timestamp`.
3. **Level 3 Focus Mode**: Lock context, freeze noise, anchor time.

---

## 9. GPU & Rendering Guardrails (Performance Contract)
To prevent Browser Main Thread exhaustion and React reconciliation saturation, redraw frequencies and dashboard complexity are strictly capped.

### 9.1 Redraw Frequency Limits
- **Tier 1**: 5Hz | **Tier 2**: 10Hz | **Tier 3**: 20Hz | **Tier 4**: 10Hz | **Tier 5**: Event-driven.
- **Golden Line Throttle**: Crosshair updates MUST be $\le 20\text{Hz}$.
- **Max Data Points**: Strictly $\le 3000$ per panel.

### 9.2 Dashboard Render Isolation Rule
To eliminate the risk of the Grafana main thread stalling during complex redraws:
- **Plane Isolation**: The **Group Wall** (High-density stream summary) MUST exist on a separate dashboard UID from the **Focus View** (Detailed analysis).
- **Loading Mechanism**: Views must be loaded via kiosk iframe or scene switch.
- **Rationale**: Grafana re-renders the entire dashboard tree on refresh; isolating the planes prevents Tier-3/4 "Essence" redraws from killing Tier-1 "Decision" latency. Hardware appliances always isolate these planes to ensure UI responsiveness.

---

## 10. Persistence & Data Contract (Implementation Mandates)
Historical persistence MUST NOT rely on visual illusion; it SHALL be backed by explicit query rules.

### 10.1 Latching & Ghost Metrics
- **Latching State**: `NORMAL → ERROR → LATCHED → NORMAL`. Use `max_over_time(stream_error_flag[5m])`.
- **Min-Hold Ghosting**: The Tier 1 health ghost value uses `min_over_time(health_score[30m])`.
- **Performance**: For 128+ streams, these SHOULD be implemented as Prometheus Recording Rules to protect dashboard responsiveness.

### 10.2 Counter Integrity
- **Precision**: All `increase()` or `sum_over_time()` queries MUST enforce **Decimals = 0** in Grafana to hide extrapolation artifacts.
- **Resets**: Standard Prometheus reset compensation is assumed; `clamp_min` is forbidden.

### 10.3 Query Downsampling
Long-range queries (>6h) MUST utilize Grafana's dynamic `$__interval` with a **Min Step of 15s** to prevent browser memory exhaustion while maintaining forensic resolution.

---

## 11. Data Integrity & Time Authority
- **Source of Truth**: **Prometheus Scrape Timestamp** is the authoritative time domain.
- **Normalization**: All encoder PCR/PTS metadata MUST be normalized relative to scrape time.
- **Temporal Sync**: URL Time Override Injection for absolute millisecond alignment across all backend queries.

---

## 12. Part II: Technical Implementation Mapping (v5.5-Grid)

This section translates the spatial tiers into the deterministic Grafana 24-column grid rendering model.

### 12.1 Grafana Coordinate System (3840 x 2160)
- **Resolution**: 3840 × 2160 (4K UHD)
- **Grid Unit**: 1 row = 8 px
- **Total Rows**: 270 (Usable: ~264 after 24px margins)
- **Panel Placement**: Defined as `(x, y, w, h)` where `x` is column start (0-23), `y` is row start, `w` is column span, and `h` is row span.

### 12.2 Master Grid Allocation
| Tier | Row Range | Function | Total Rows |
| :--- | :--- | :--- | :--- |
| **Tier 1** | 0 – 24 | Failure Domain Command | 24 |
| **Tier 2** | 24 – 64 | Hybrid Diagnostic Matrix | 40 |
| **Tier 3** | 64 – 172 | Essence Analysis Core | 108 |
| **Tier 4** | 172 – 228 | Predictive Horizon | 56 |
| **Tier 5** | 228 – 264 | Operational Audit Log | 36 |

### 12.3 Tier Implementation Details

#### Tier 1 — Command Bar
- **Signal Lock LED**: `x:0, y:0, w:2, h:24` (Stat Panel, Background Solid)
- **Failure Domain Banner**: `x:2, y:0, w:10, h:24` (Text/Stat, PromQL: `dominant_failure_domain`)
- **Master Health Score**: `x:12, y:0, w:4, h:24` (Gauge + Threshold)
- **Determinism Metrics**: `x:16, y:0, w:4, h:24` (`worker_overrun`, `internal_drop`)
- **Clock + Focus State**: `x:20, y:0, w:4, h:24` (Text: `🔒 STREAM FOCUS LOCKED`)

#### Tier 2 — Diagnostics Matrix (2x4)
Each LED tile: `w:3, h:10`. Gap: 1 column.
- **Network (Left)**: Capacity `(x:6, y:24)`, NAK `(x:6, y:34)`, Buffer `(x:6, y:44)`, RTT `(x:6, y:54)`
- **TR 101 290 (Right)**: P1.1 `(x:11, y:24)`, P1.3 `(x:11, y:34)`, P1.5 `(x:11, y:44)`, P1.4 `(x:11, y:54)`

#### Tier 3 — Essence Core
- **Bitrate Envelope**: `x:0, y:64, w:15, h:108` (Time Series, `avg_over_time`, `max/min` envelope)
- **Content Quality (Stacked RIGHT, w:9)**:
  - FPS Stability: `y:64, h:34`
  - GOP Cadence: `y:98, h:34`
  - AV Sync: `y:132, h:40`

#### Tier 4 — Predictive Horizon
- **RST Survival Timeline**: `x:0, y:172, w:11, h:56` (Derived: `buffer_margin / depletion_rate`)
- **PID Inventory**: `x:11, y:172, w:13, h:56` (Stacked Area, Null PID bottom)

#### Tier 5 — Forensic Audit
- **Audit Log**: `x:0, y:228, w:24, h:36` (Logs Panel)

### 12.4 Performance & Interaction Hard Limits
| Rule | Requirement |
| :--- | :--- |
| **Max Queries/Panel** | $\le 6$ |
| **Total Dashboard Panels** | $\le 28$ |
| **Default Refresh Rate** | 5s |
| **Max TimeSeries Points** | $\le 3000$ |
| **Shared Crosshair** | MANDATORY (ON) |
| **Forensic Data Link** | `/d/forensic?time=${__value.time}` (Millisecond Alignment) |
