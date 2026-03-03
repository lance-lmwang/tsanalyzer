# TsAnalyzer Pro NOC Dashboard Visual Spec

This specification defines the visual, operational, and spatial standards for the TsAnalyzer Pro NOC. It follows a **Three-Plane Appliance Architecture** designed to transition an operator from fleet-wide situational awareness to millisecond-accurate forensic evidence.

---

## 0. Operational Surface Overview (Three-Plane Model)

To ensure UI responsiveness and operational focus, the NOC is divided into three isolated functional planes:

| Plane | Function | Cognitive Role |
| :--- | :--- | :--- |
| **Plane 1** | **Global Stream Wall** | Fleet Situation Awareness |
| **Plane 2** | **Stream Focus View** | Diagnostic & Prediction |
| **Plane 3** | **Forensic Memory** | Bit-Exact Evidence Audit |

---

## 1. Tier 0: Global Stream Wall (Plane 1 Architecture)

The Global Stream Wall is the primary entry point. It uses a mosaic packing algorithm to maximize visual occupancy while minimizing operator eye-scan latency.

### 1.1 The Mosaic Packing Algorithm (Constant Human Load)
The wall dynamically calculates the optimal $cols \times rows$ layout based on the current `stream_count` and the screen aspect ratio ($3840/2160 = 1.78$).

**Mathematical Model:**
- $cols = \lceil \sqrt{N \times 1.78} \rceil$
- $rows = \lceil N / cols \rceil$

### 1.2 Operational State Semantics (Appliance Logic)
Tiles display **Operational States**, not just raw alarms.

| Color | Meaning | Description |
| :--- | :--- | :--- |
| 🟢 | **Stable** | Signal Fidelity $\ge 90\%$. |
| 🟡 | **Degraded** | Signal Fidelity $70\% - 90\%$. |
| 🔴 | **Critical** | Signal Fidelity $1\% - 70\%$. |
| ⚫ | **No Signal** | No transport packets detected for $> 5\text{s}$. |
| 🔵 | **Engine Drop** | Internal analyzer overflow (Internal Drop $> 0$). |

### 1.3 Navigation: The Focus Trigger
Each tile is an interactive portal:
- **Action**: Click Stream Tile.
- **Impact**: Instantly transitions from Plane 1 (Fleet) to Plane 2 (Focus) with localized context.

---

## 2. Plane 2: Stream Focus View (The 7-Tier Architecture)

The Focus View is a **Zero-Scroll** interface optimized for **4K UHD (3840x2160)**. It follows a strict top-to-bottom eye-scan path organized by decision latency.

### 1.1 Spatial Grid Tiers

| Tier | Function | Grid Position (Y) | Component Logic |
| :--- | :--- | :--- | :--- |
| **Tier 1** | **Master Control Console** | 0 – 4 | Executive signal presence and fidelity bar. |
| **Tier 2** | **Transport & Link Integrity** | 5 – 8 | Hybrid SRT/MDI diagnostic matrix. |
| **Tier 3** | **ETR 290 P1 (Critical)** | 9 – 12 | TR 101 290 Priority 1 compliance monitoring. |
| **Tier 4** | **ETR 290 P2 (Timing)** | 13 – 17 | Clock accuracy and PCR/PTS timing stability. |
| **Tier 5** | **Service Payload Dynamics** | 18 – 33 | Mux inventory and PID bitrate distribution. |
| **Tier 6** | **Essence Quality** | 34 – 43 | GOP structure, FPS stability, and AV sync. |
| **Tier 7** | **Alarm Recap & Forensic** | 44 – 60+ | Operational audit trail and alarm history. |

---

## 3. Tier 1: Master Control Console (SIGNAL STATUS)
The executive signal status bar for instant health assessment.
- **Signal Presence**: Binary indicator (LOCKED/LOST) based on sync byte consistency.
- **Signal Fidelity**: Gauge (0-100%) derived from CC errors, jitter, and RST.
- **Engine Determinism**: Real-time tracking of `internal_analyzer_drop` and `worker_slice_overruns`.

## 4. Tier 2: Transport & Link Integrity (SRT/MDI)
Hybrid matrix correlating network-layer metrics with stream-layer health.
- **SRT Metrics**: RTT, NAKs, and Retransmit Tax.
- **Network Jitter**: MDI Delay Factor (DF) and Media Loss Rate (MLR).

## 5. Tier 3: ETR 290 P1 (CRITICAL COMPLIANCE)
Real-time monitoring of TR 101 290 Priority 1 errors.
- **Sync Loss / Byte Error**: Physical layer integrity.
- **PAT / PMT Error**: Service-level mapping integrity.
- **CC Error**: Packet loss detection at the elementary stream level.

## 6. Tier 4: ETR 290 P2 (CLOCK & TIMING)
Metrology of the System Time Clock (STC) and PCR stability.
- **PCR Accuracy**: Jitter vs. established STC slope.
- **PCR Repetition**: Interval between timing markers (Max 40ms).

## 7. Tier 5: Service Payload Dynamics (MUX)
Analysis of the multiplex structure and bitrate revenue.
- **PID Bitrate Revenue**: Stacked density view of all active PIDs.
- **Null Packet Density**: Percentage of stuffing (Idle capacity).

## 8. Tier 6: Essence Quality & Temporal Stability
Deep inspection of the decoded elementary stream parameters.
- **Frame Rate Stability**: Consistency of frame intervals (FPS).
- **GOP Cadence**: Temporal interval between I-frames (ms).
- **Lip-Sync Offset**: AV synchronization (PTS delta in ms).

## 9. Tier 7: Alarm Recap & Forensic Audit Trail
Chronological event log and forensic evidence alignment.
- **Alarm Recap**: 1h rolling window of CC and compliance events.
- **Forensic Sync**: Clicking any event anchors the Plane 3 Forensic View to that exact millisecond.

---

## 10. GPU & Rendering Guardrails (Performance Contract)
To prevent Browser Main Thread exhaustion, redraw frequencies are strictly capped.

### 10.1 Redraw Frequency Limits
- **Tiers 1–4**: 5Hz (High-priority decision zone).
- **Tiers 5–6**: 2Hz (Analysis zone).
- **Tier 7**: Event-driven.
- **Max Data Points**: Strictly $\le 3000$ per panel.

### 10.2 Dashboard Render Isolation Rule
To eliminate the risk of the Grafana main thread stalling during complex redraws:
- **Plane Isolation**: The **Global Wall** MUST exist on a separate dashboard UID from the **Stream Focus**.
- **Rationale**: Isolating the planes prevents Tier 5/6 "Essence" redraws from killing Tier 1 "Decision" latency.

---

## 11. Persistence & Data Contract (Implementation Mandates)

### 11.1 Latching & Alarm Thresholds
- **Critical Alarm**: Latched for 5m via `max_over_time`.
- **Global Wall Thresholds**: 90% (Stable), 70% (Degraded), 1% (Critical), 0% (Lost).

---

## 12. Part II: Technical Implementation Mapping (7-Tier Grid)

### 12.1 Master Grid Allocation (24-Column Model)
| Tier | Row Range (Y) | Function | Total Rows |
| :--- | :--- | :--- | :--- |
| **Tier 1** | 0 – 4 | Master Control Console | 5 |
| **Tier 2** | 5 – 8 | Transport & Link Integrity | 4 |
| **Tier 3** | 9 – 12 | ETR 290 P1 (Critical) | 4 |
| **Tier 4** | 13 – 17 | ETR 290 P2 (Clock) | 5 |
| **Tier 5** | 18 – 33 | Service Payload Dynamics | 16 |
| **Tier 6** | 34 – 43 | Essence Quality | 10 |
| **Tier 7** | 44 – 60+ | Alarm Recap & Forensic | 16+ |

### 12.2 Tier Implementation Details

#### Tier 1 — Master Control Console
- **Signal Presence**: `x:0, y:0, w:6, h:4`
- **Signal Fidelity**: `x:6, y:0, w:12, h:4`
- **Engine Determinism**: `x:18, y:0, w:6, h:4`

#### Tier 2 — Transport Layer
- **SRT/MDI Matrix**: `x:0, y:5, w:24, h:3`

#### Tier 3 — ETR 290 P1
- **Critical Compliance Row**: `x:0, y:9, w:24, h:3`

#### Tier 4 — ETR 290 P2
- **Clock & Timing Row**: `x:0, y:13, w:24, h:4`

#### Tier 5 — Payload Dynamics
- **PID Bitrate Stacked**: `x:0, y:18, w:18, h:16`
- **Null Density Gauge**: `x:18, y:18, w:6, h:16`

#### Tier 6 — Essence Quality
- **FPS Stability**: `x:0, y:34, w:8, h:10`
- **GOP Cadence**: `x:8, y:34, w:8, h:10`
- **Lip-Sync Offset**: `x:16, y:34, w:8, h:10`

#### Tier 7 — Alarm Recap
- **Audit Table**: `x:0, y:44, w:24, h:16`


### 12.4 Performance & Interaction Hard Limits
| Rule | Requirement |
| :--- | :--- |
| **Max Queries/Panel** | $\le 6$ |
| **Total Dashboard Panels** | $\le 28$ |
| **Default Refresh Rate** | 5s |
| **Max TimeSeries Points** | $\le 3000$ |
| **Shared Crosshair** | MANDATORY (ON) |
| **Forensic Data Link** | `/d/forensic?time=${__value.time}` (Millisecond Alignment) |
