# TsAnalyzer Pro NOC Dashboard Visual Spec (Ultimate v5.4 - Expert Ops Edition)

This specification defines the visual and operational standards for the TsAnalyzer Pro NOC Dashboard. It transforms standard monitoring into an **Instrumented Cognitive NOC**—a system where protocol truth directly drives operational decision-making.

The system treats **Operator Cognition** as a constrained system resource, optimizing for decision latency under stress across a 4K UHD layout.

---

## 0. Appliance Scaling Contract (The Human Limit)
The dashboard SHALL maintain a maximum direct visualization density of **$\le 128$ streams per view** (The Operator Decision Bandwidth Limit). Scaling beyond 128 streams MUST be achieved through **hierarchical fleet aggregation**.

---

## 1. Progressive Observability (The 3-Level NOC Hierarchy)
To prevent visual failure during massive events, the dashboard implements a three-tier cognitive funnel.

### Level 1: Fleet Summary (Spatial Awareness Layer)
- **Topology Modes**: Site Map (Geographic) or Rack/Network Mode (Infrastructure mapping).
- **Fleet Instability Indicator**: A global gauge showing `Latched Streams (%)` (Formula: `count(stream_error_latched_5m) / count(total_streams)`).
  - *Actionable Metric*: Convert noise into trend awareness. High percentage indicates systemic failure.

### Level 2: Group Wall (The Active Viewport)
- **Triage Matrix**: A high-density grid of micro-LEDs (up to 128).
- **Failure Wave Propagation**: When multiple failures occur, the Matrix MUST visually animate the cascade (ripple effect). The animation MUST originate from the `first_error_timestamp(stream_group)` to prevent causality illusions.
- **Anomaly Persistence**: Triage LEDs MUST remain AMBER for 5 minutes (LATCHED state) after recovery.

### Level 3: Stream Focus Mode (Deep Dive)
- **Operational Context Switch**: Lock selected stream, freeze background noise, and anchor time.
- **Cognitive Lock Indicators**: UI MUST prominently display:
  - `🔒 STREAM FOCUS LOCKED`
  - `Operator: <Operator_ID>` | `Duration: <Timer>`
  - `Noise: FROZEN` | `Alerts Suppressed: <Counter>`
- This ensures the operator remains in a forensic sandbox, isolated from live fleet refresh noise.

---

## 2. Tier 1: Failure Domain Banner & Signal Lock
This top-level row provide automated failure attribution to bypass manual operator deduction.
- **Signal Lock LED**: 120x120px indicator (LOCKED #1C8C5E / LOST #C0392B).
- **Failure Domain Banner**: A dynamic text banner using **Weighted Inference Correlation**.
  - *Network Probability*: `(P1.4 * 0.4) + (SRT_NAK * 0.3) + (RTT_Var * 0.3)`
  - *Encoder Probability*: `(P1.3 * 0.4) + (P1.5 * 0.4) + (FPS_Instability * 0.2)`
  - *Display*: `DOMINANT FAILURE DOMAIN: <Type> (<Confidence_Score>%)`.
- **Master Health Score**: 0-100 score with **Min-Hold logic** (30% opacity ghost value for 30m minimum).

---

## 3. Tier 2: Hybrid Analytics Matrix (Diagnostic Core)
Fixed 2x4 LED grid. Clicking an error block opens the Forensic Log.
- **Pattern Recognition Mode**: Specific error combinations (e.g., P1.4 + NAK spike) trigger an **AUTO TAG** overlay (e.g., `NETWORK IMPAIRMENT`) and a frame pulse instead of generating a new alert.

| Slot | Left Group: SRT/Network | Right Group: TS Compliance (TR 101 290 P1) | Operator Implicit Action |
| :--- | :--- | :--- | :--- |
| **Row 1** | Link Capacity (Headroom) | **P 1.1: TS Sync Loss** | Check Physical Link / Signal Presence |
| **Row 2** | NAK Count (Loss Events) | **P 1.3: PAT Error** | Check Muxer / PSI Table Cycle |
| **Row 3** | Buffer Margin (ms) | **P 1.5: PMT Error** | Check Stream Mapping / SI Integrity |
| **Row 4** | RTT Variance (Jitter) | **P 1.4: CC Error** | Correlate with SRT NAKs (Network Loss) |

---

## 4. Tier 3: Bitrate & Essence Vitals (Historical Timelines)
All charts share a **Locked X-Axis** for forensic correlation.

### 4.1 24-Hour Bitrate Envelope
- **Visualization**: Average line with shaded Min/Max envelope.
- **Volatility Encoding**: Envelope opacity MUST scale with volatility (wider envelope = higher opacity).
- **Dual-Trace**: Physical Bitrate (Cyan Solid) vs. PCR Bitrate (White Dashed). Gap = Packet Arrival Jitter.

### 4.2 Content Quality Sparklines
- **FPS Stability**: Line chart with fixed reference lines and a **Red Minimum-Value Trace**.
- **GOP Cadence**: Historical trend of I-frame intervals (ms).
- **AV Sync / Lip-Sync**: Dual-polarity PTS offset history (+/- 200ms).

---

## 5. Tier 4: Predictive Horizon & PID Inventory
- **RST Survival Timeline**: Visual history of buffer safety margins.
- **Time to Failure Estimate**: Dynamic countdown calculated as `Buffer_Margin / Depletion_Rate` (e.g., `⚠ Buffer Exhaustion ≈ 42s`).
- **PID Time-Series**: 24-hour stacked area chart for component distribution.

---

## 6. Tier 5: Operational Audit Trail (Forensic Log)
- **Point-in-Time Drill-Down**: Clicking any dip or error scrolls the log to that millisecond.
- **Micro-Capture Link**: Access to the 200ms raw TS snippet recorded during P1 triggers.

---

## 7. Visual Standards & Browser Guardrails
- **Layout**: **Zero-Scroll** optimized for **3840x2160 (4K UHD)**.
- **Performance**: Golden Line throttle $\le 20\text{Hz}$ (Matches human analytical tracking limits); Max 3000 data points per panel.
- **Palette**: Background `#020617`, Panels `#0F172A`, Accent `#38BDF8`.

---

## 8. Persistence & Data Contract
- **Time Authority**: **Prometheus Scrape Timestamp** is the single source of truth. All encoder PCR/PTS metadata MUST be normalized relative to scrape time.
- **Temporal Sync**: URL Time Override Injection for absolute millisecond alignment.
- **Counter Safety**: Use `increase()` with Grafana **Decimals = 0**.

---

## 9. Industry Positioning
`tsa_server_pro` graduates to a **Cognitive Operating System for Broadcast**. It moves from passive monitoring to automated diagnostic assistance, rivaling dedicated hardware metrology appliances.
