# TsAnalyzer Pro NOC Dashboard Visual Spec (Ultimate v4.2 - Persistence Edition)

This specification defines the visual and operational standards for the TsAnalyzer Pro NOC Dashboard. It prioritizes **Historical Anomaly Persistence** to ensure that intermittent monitoring (e.g., checking every 30 minutes) successfully identifies transient stream failures.

---

## 0. Tier 0: Global Fleet Triage Center (The Command Wall)
Instantaneous identification of failures among thousands of streams.
- **Anomaly Persistence**: If a stream recovers from an error, its triage LED MUST remain AMBER for 5 minutes (Latching) to notify roving operators of a recent incident.

---

## 1. Layout Philosophy: The "Fixed Glass" Rule
- **Zero-Scroll**: Single-page interface optimized for 1920x1080.
- **Proportions**: Tier 1 (12%) / Tier 2 (18%) / Tier 3 (20%) / Tier 4 (32%) / Tier 5 (18%).
- **Forensic Time-Sync**: Clicking any point in history synchronizes ALL Tier 1-5 charts to that precise millisecond.

---

## 2. Tier 1: Appliance Vitality & Signal Lock
- **Master Health Score**: 0-100 aggregate score with **Min-Hold logic** (The lowest score in the last 30 minutes is displayed as a ghost value below the current score).

---

## 3. Tier 2: Hybrid Analytics Matrix (Diagnostic Core)
- **Latching Error Counters**: Error blocks (CC, PAT, PMT) show the total error count over a 60-minute sliding window, ensuring transient errors are visible during periodic checks.

---

## 4. Tier 3: Bitrate & Essence Vitals (Historical Timelines)
This tier is designed to expose "spikes and dips" that occurred while the operator was away.

### 4.1 24-Hour Bitrate Envelope
- **Persistence**: Shaded Min/Max area sampled every minute. A sharp "narrowing" or "widening" of the envelope indicates a historical bitrate anomaly.

### 4.2 Content Quality Timelines (With Anomaly Highlighting)
- **FPS Stability**:
    - **Visualization**: Line chart with fixed 25/50/60 reference lines.
    - **Persistence**: Employs a **"Minimum-Value Trace"** (Red line) that tracks the lowest FPS seen in the window. If FPS dropped to 5 and recovered, the Red Trace remains at the bottom of the dip for visibility.
- **GOP Cadence**: Historical trend of I-frame intervals. Spikes in the trend line indicate encoder stalls or scene-change resets.
- **AV Sync / Lip-Sync**: Dual-polarity PTS offset history. Deviations from the 0ms center line are permanently recorded in the timeline.

---

## 5. Tier 4: Predictive Horizon & PID Inventory
- **RST Survival Timeline**: Shows the history of the "Remaining Safe Time." If the buffer almost emptied 15 minutes ago, the dip will be visible on this chart.
- **PID Time-Series**: Tracks bitrate stability of individual components (Video vs. Audio) over a 24-hour window.

---

## 6. Tier 5: Operational Audit Trail (Forensic Log)
- **Log Retention**: 5,000 entries with millisecond timestamps.
- **Visual Mapping**: Error logs are color-coded to match the "Persistence" highlights in Tier 3 (e.g., Red log entries for the exact time the FPS dip occurred).

---

## 7. Visual Standards
- **Palette**: Background `#020617`, Panels `#0F172A`.
- **Primary Accent**: `#38BDF8` (Cyan).
- **Forensic Highlight**: A **Vertical Golden Line** marks the point of interest across all charts during historical drill-down.
- **Hardware Texture**: 5% opacity white grid overlay.
