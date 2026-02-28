# Professional NOC Dashboard & QoE Design (Telestream Benchmark)

This document defines the high-end monitoring architecture for TsAnalyzer Pro, benchmarked against **Telestream Surveyor** and **srt_monitor**, focusing on tiered diagnostics and intuitive visualization.

---

## 1. Professional Data Layer (Metric Tiering)

We move away from a flat metric list to a **five-tier diagnostic model**:

### Tier 1: Executive Signal Lock (QoS)
- **Stream Status**: (LOCKED / SIGNAL LOST) - High-level binary state.
- **Link Saturation**: Real-time comparison of current bitrate vs. allocated link bandwidth.
- **SRT Safety Margin**: Current buffer level (ms) vs. configured latency.

### Tier 2: ETR 290 Compliance (The Matrix)
*Instead of one line, we use a 2x4 Matrix with real-time counters.*
- **Priority 1 (Fatal)**:
    - `tsa_sync_loss_errors`: Sync Loss count (Delta 60s).
    - `tsa_pat_error_count`: PAT Missing/Timeout (Immediate).
    - `tsa_pmt_error_count`: PMT Missing/Timeout (Immediate).
    - `tsa_cc_errors_total`: **Continuity Error Count** (Real-time delta, NOT just boolean).
- **Priority 2 (Performance)**:
    - `tsa_pcr_repetition_errors`: PCR Interval issues.
    - `tsa_pcr_accuracy_errors`: PCR Jitter/Drift issues.
    - `tsa_crc_errors`: Section CRC mismatch.

### Tier 3: TS Mux & Payload Analysis (The Spectrum)
- **Muxrate Envelope**:
    - **Physical (SRT)**: Thick Cyan line.
    - **Logical (TS)**: Dashed White line.
    - **Effective Payload**: Area stack of Video/Audio PIDs.
    - **Stuffing (Null)**: Dark Blue area stack at the bottom (shows explicitly how much bandwidth is "wasted").
- **PID Inventory**: A sortable table with micro-gauges for every active PID.

### Tier 4: ES Vital & QoE (Quality of Experience)
- **Video Cadence**: FPS (Frame rate) and GOP size consistency.
- **Lip-Sync**: AV-Sync Offset (ms) with threshold markers.
- **Content Health**: Black Frame / Freeze Frame duration counters.

---

## 2. NOC "Stream Wall" Design (Tier 0)

Designed for massive-scale monitoring (100+ streams) on a single high-res display.

### 2.1 The Heatmap Matrix
- **Grid Layout**: 40x40px cells representing each stream.
- **State Latching**:
    - **Green**: Optimal.
    - **Blinking Red**: New P1 Error (< 30s).
    - **Solid Red**: Persistent P1 Error (> 30s).
    - **Amber**: Acknowledged (ACK) Error.
- **Micro-Sparklines**: A 1px wide line at the bottom of each cell showing 60s health trend.

### 2.2 Top-N Offender Shelves (Auto-Focus)
- **Top 5 Worst MDI-DF**: Identify network congestion points instantly.
- **Top 5 Highest CC Loss**: Identify specific encoder/path degradation.

---

## 3. Advanced RCA: Fault Correlation Logic

Telestream-grade logic to distinguish source vs. network:

| Observed Signal | RCA Conclusion | Action Needed |
| :--- | :--- | :--- |
| MDI-DF Spike + CC Errors + No PCR Jitter | **Network Congestion** | Increase Pacer Buffer / Check Path |
| Stable MDI + CC Errors + PCR Jitter | **Encoder Instability** | Restart Transcoder / Check Source |
| Stable Transport + Stagnant PTS (Freeze) | **Upstream Source Issue** | Escalate to Content Provider |

---

## 4. Automation & Automation Gates

### 4.1 "Time-Machine" Trigger
When any Tier 2 Priority 1 error is detected:
1. Auto-trigger **Forensic Capture** (30s window).
2. Generate `manifest.json` with **RCA Attribution**.
3. Send **Webhook** with incident priority.

### 4.2 Proactive SLA Reporting
Daily PDF reports summarizing:
- **Total Availability**: (Time Locked / Total Time) %.
- **Compliance SLA**: Percentage of time with zero P1 errors.
- **MTTR**: Mean Time to Repair metrics for NOC efficiency.
