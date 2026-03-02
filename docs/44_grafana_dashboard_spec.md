# TsAnalyzer Pro NOC Dashboard Visual Spec (Ultimate v5.3 - Instrumented Cognitive Edition)

This specification defines the visual and operational standards for the TsAnalyzer Pro NOC Dashboard. Designed as a **Broadcast-Grade Appliance NOC** with a hard limit of **128 concurrent streams**, it transforms standard monitoring into an **Instrumented Cognitive NOC**—a system where protocol truth directly drives operational decision-making. 

The system prioritizes **TR 101 290 P1 Compliance**, **Hierarchical Forensic Drill-Down**, and **Automated Failure Domain Correlation** across a 4K UHD layout.

---

## 0. Appliance Scaling Contract (The Human Limit)
The dashboard SHALL maintain a maximum direct visualization density of **$\le 128$ streams per view** (The Operator Decision Bandwidth Limit). Scaling beyond 128 streams MUST be achieved through **hierarchical fleet aggregation**.

---

## 1. Progressive Observability (The 3-Level NOC Hierarchy)
To prevent visual failure during massive events, the dashboard implements a three-tier cognitive funnel.

### Level 1: Fleet Summary (Spatial Awareness Layer)
- **Topology Modes**: Site Map (Geographic) or Rack/Network Mode (Infrastructure mapping).
- **Fleet Instability Indicator**: A global gauge showing `Streams in LATCHED state (%)` per topological group. 
  - *Actionable Metric*: 1 Amber = Investigate, 20 Amber in one Rack = Systemic Hardware Failure.

### Level 2: Group Wall (The Active Viewport)
- **Triage Matrix**: A high-density grid of micro-LEDs (up to 128).
- **Failure Wave Propagation**: Matrix MUST visually animate cascades (e.g., ripple effects) to indicate blast radius origins.
- **Anomaly Persistence**: Triage LEDs MUST remain AMBER for 5 minutes (LATCHED state) after recovery.

### Level 3: Stream Focus Mode (Deep Dive)
- **Operational Context Switch**: Lock selected stream, freeze background noise, and anchor time.
- **Cognitive Lock Indicator**: UI MUST display: `🔒 STREAM FOCUS LOCKED`, `Noise: FROZEN`, `Time: ANCHORED`.

---

## 2. Tier 1: Failure Domain Banner & Signal Lock
This top-level row provides the ultimate "Decision Shortcut" for the operator.
- **Signal Lock LED**: 120x120px indicator (LOCKED #1C8C5E / LOST #C0392B).
- **Failure Domain Banner**: A dynamic text banner that appears during active P1 alarms.
  - *Logic*: Correlates P1.4 (CC Error) with SRT NAKs and RTT Variance.
  - *Example Display*: `DOMINANT FAILURE DOMAIN: NETWORK (82%)` or `DOMINANT FAILURE DOMAIN: ENCODER (91%)`.
- **Master Health Score**: 0-100 aggregate score with **Min-Hold logic** (30% opacity ghost value for 30m minimum).
- **Determinism metrics**: Internal Drops and Worker Overruns.

---

## 3. Tier 2: Hybrid Analytics Matrix (TR 101 290 P1 Diagnostic Core)
Fixed 2x4 LED grid for instant pattern recognition. Clicking an error block opens the Forensic Log.

| Slot | Left Group: SRT/Network | Right Group: TS Compliance (TR 101 290 P1) | Operator Implicit Action |
| :--- | :--- | :--- | :--- |
| **Row 1** | Link Capacity (Headroom) | **P 1.1: TS Sync Loss** | Check Physical Link / Signal Presence |
| **Row 2** | NAK Count (Loss Events) | **P 1.3: PAT Error** | Check Muxer / PSI Table Cycle |
| **Row 3** | Buffer Margin (ms) | **P 1.5: PMT Error** | Check Stream Mapping / SI Integrity |
| **Row 4** | RTT Variance (Jitter) | **P 1.4: CC Error** | Correlate with SRT NAKs (Network Loss) |

---

## 4. Tier 3: Bitrate & Essence Vitals (Historical Timelines)
Primary area for identifying fluctuations. All charts share a **Locked X-Axis**.

### 4.1 24-Hour Bitrate Envelope & Jitter
- **Implementation**: Uses `max_over_time` and `min_over_time` to render a shaded envelope around the average.
- **Dual-Trace**: Physical Bitrate (Cyan Solid) vs. PCR Bitrate (White Dashed). Gap = Packet Arrival Jitter (MDI).

### 4.2 Content Quality Sparklines
- **FPS Stability**: Line chart with 25/50/60 reference lines and a **Red Minimum-Value Trace**.
- **GOP Cadence**: Historical trend of I-frame intervals (ms).
- **AV Sync / Lip-Sync**: Dual-polarity PTS offset history (+/- 200ms).

---

## 5. Tier 4: Predictive Horizon & PID Inventory
- **RST Survival Timeline**: Visual history of buffer depletion safety margins.
- **PID Time-Series**: 24-hour stacked area chart for component distribution (including Null Packet Stuffing).

---

## 6. Tier 5: Operational Audit Trail (Forensic Log)
- **Point-in-Time Drill-Down**: Clicking any dip or error automatically scrolls the log to that millisecond.
- **Micro-Capture Link**: Direct access to the 200ms raw TS snippet recorded during P1 triggers.

---

## 7. Visual Standards & Browser Guardrails
- **Layout**: **Zero-Scroll** optimized for **3840x2160 (4K UHD)**.
- **Performance**: Golden Line throttle $\le 20\text{Hz}$; Max 3000 data points per panel.
- **Palette**: Background `#020617`, Panels `#0F172A`, Accent `#38BDF8`.

---

## 8. Persistence & Data Contract
- **Latching**: `max_over_time(stream_error_flag[5m])`.
- **Counter Safety**: Use `increase()` with Grafana **Decimals = 0**.
- **Time Authority**: **Prometheus Scrape Timestamp** is the single source of truth. All encoder PCR/PTS metadata MUST be normalized relative to scrape time.
- **Temporal Sync**: URL Time Override Injection for absolute millisecond alignment across metrics and logs.

---

## 9. Industry Positioning
`tsa_server_pro` moves beyond standard Dashboards. By bridging raw protocol truth with automated failure correlation and operational memory, it occupies the **Instrumented Cognitive NOC** tier, rivaling dedicated hardware metrology appliances.
