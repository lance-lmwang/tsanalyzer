# TsAnalyzer Pro NOC Dashboard Visual Spec (Ultimate v3.5)

This specification defines the ultimate visual and operational standards for the TsAnalyzer Pro NOC Dashboard, strictly aligned with professional broadcast monitoring hardware like **Tektronix Sentry** and **Rohde & Schwarz PRISMON**.

---

## 0. Tier 0: Global Fleet Triage Center (The Command Wall)
The Fleet Triage Center is the triage station for massive-scale observability (1000+ streams). It MUST provide an instantaneous understanding of global health.

### 0.1 Color Dominance & LED States
- **EMERGENCY (Red/Square)**: Persistent Signal Loss, Stream Freeze, or Network RST < 5s.
- **CRITICAL (Orange/Triangle)**: Network RST < 10s or Active TR 101 290 P1 Errors.
- **OPTIMAL (Green/Circle)**: Stable streams.
- **Soft Blink Definition**: Opacity Cycle (100% -> 70%) at 2Hz. Prohibit Black-to-Red hard toggle.

### 0.2 Sorting & Pinning Priority
1. **Manual Pin (Sticky Pin)**: Manually pinned streams always stay at the top.
2. **Lowest Remaining Safe Time (RST)**.
3. **Highest P1 CC Error Count** within the last 60s sliding window.

---

## 1. Layout Philosophy: The "Fixed Glass" Rule
The dashboard MUST be a **Zero-Scroll** interface optimized for 1920x1080 viewports. 
- **Absolute Proportions**: Tier 1 (12%) / Tier 2 (18%) / Tier 3 (20%) / Tier 4 (32%) / Tier 5 (18%).
- **Hard Lock**: No content expansion is permitted to trigger a browser scrollbar. Dynamic content (PID lists, Logs) MUST use internal scroll containers.

---

## 2. Tier 1: Link Survival & Signal Lock (The Decision Bar)
This top-level row defines if the "Pipe" is functional. It is the primary decision-making area.
- **Signal Lock Status**: A 120x120px pure color LED (LOCKED: #1C8C5E / LOST: #C0392B). Binary state, no gradients.
- **Master Health Score**: 0-100 aggregate score based on the piecewise deterministic model. Uses the **Lid Rule** (Capped at 60 if signal is LOST or UNREC LOSS > 0).
- **SRT Transmission Vitals (Link Integrity)**:
    - **RTT (ms)**: Round Trip Time (Link distance and delay).
    - **RETR (%)**: ARQ Retransmission rate (Packet recovery overhead).
    - **UNREC LOSS**: Packet drops that ARQ could not fix. MUST turn RED if > 0.

---

## 3. Tier 2: Hybrid Analytics Matrix (Diagnostic Core)
A fixed 2x4 LED grid combining SRT Transport and TS Compliance layers. Operators rely on **Spatial Memory** to identify faults instantly.

| Slot | Left Group: SRT Transport | Right Group: TS Compliance |
| :--- | :--- | :--- |
| **Row 1** | Link Capacity (Bandwidth Headroom) | Sync Lock (P1.1) |
| **Row 2** | NAK Count (Loss detection frequency) | PAT Error (P1.2) |
| **Row 3** | Buffer Margin (Safety Window) | PMT Error (P1.3) |
| **Row 4** | RTT Variance (Jitter intensity) | CC Error (P1.4) |

### 3.1 Smart-Switch & Latching Logic
- **Normal State**: Deep Green Block with white "OK".
- **Error State**: Bright Red Block with **Actual Error Count** (Delta over 60s).
- **ACK/Latching**: If an operator ACKs a fault, the block turns **Solid Amber (#F39C12)** until the condition clears.

---

## 4. Tier 3: Essence Vitals (The Metadata Row)
Compact stability trends for encoding consistency. Four sparklines with **shared X-axis alignment (Timeline Lock)**.
- **FPS Stability**: Line chart with locked 25.00 reference line. Vertical displacement indicates jitter.
- **GOP Consistency**: Measured in ms (Consistency of I-Frame intervals). Rendered as a horizontal trend.
- **AV Sync / Lip-Sync**: Dual-polarity (0-center) PTS drift chart. Fixed Y-axis at +/-200ms.
- **Dual Bitrate Comparison**:
    - **Actual Bitrate (Physical)**: Solid Cyan Line (#06B6D4). Represents aggregate bits arriving at the interface.
    - **PCR Bitrate (Muxrate)**: White Dashed Line. Represents the logical clock bitrate.
    - **Interpretation**: The gap between lines indicates **Packet Arrival Jitter**.

---

## 5. Tier 4: Predictive Analytics & PID Inventory
Analysis of the "Predictive Survival Horizon" and sub-component breakdown.
- **RST Survival**: Predicted seconds remaining for Network/Encoder buffers (The "10s Warning" system).
- **MDI-DF Trend**: 60s history of jitter vs. buffer limit. (Max Height: 80px).
- **PID Inventory Table**: High-density list with PID, Type (V/A/T/D), Bitrate, and BW%.
- **PID Time-Series Chart**: Large multi-line chart (Vid/Aud/Null) for sub-component stability tracking.

---

## 6. Tier 5: Operational Audit Trail (Forensic Log)
A high-density table for sequential forensic analysis.
- **Emergency (P0)**: White bold text on a Red background.
- **Critical (P1)**: Orange text on transparent background.
- **Notice (P2)**: Gray text.
- **Engineering Constraint**: UI table MUST cap at **5,000 entries** using a FIFO buffer.

---

## 7. Visualization Standards
- **Primary Background**: `#020617` (Deep Black - "Lights-out" NOC feel).
- **Secondary Background**: `#0F172A` (Deep Slate - Panel contrast).
- **Safety Green**: `#2ECC71`.
- **Emergency Red**: `#FF4D4D`.
- **Warning Amber**: `#F39C12`.
- **Grid Overlay**: 5% transparency white lines for "Hardware Analyzer" texture.
