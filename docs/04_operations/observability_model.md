# 7-Layer NOC Observability Model

The TsAnalyzer Appliance employs a hierarchical observability model designed to translate dozens of raw technical metrics into high-level operational insights. This ensures that NOC operators can make rapid decisions without being overwhelmed by low-level metrology data.

---

## 1. The Observability Layers

Each layer answers a progressively higher-level question about the health and availability of the stream.

### Layer 1: Physical Transport Integrity
*   **Question**: Is the stream physically arriving and intact?
*   **Metrics**: TS Sync Byte, Continuity Counter (CC) sequence, Transport Error Indicator (TEI), Packet Loss.
*   **Primary Goal**: Detecting IP network drops, NIC overflows, or multicast routing issues.

### Layer 2: Structural Integrity (SI/PSI)
*   **Question**: Can the decoder understand the stream structure?
*   **Metrics**: Table repetition intervals (PAT/PMT/NIT/SDT), CRC32 validity, Service mapping consistency.
*   **Primary Goal**: Identifying multiplexer configuration errors or missing signaling tables.

### Layer 3: Timing & Clock Health
*   **Question**: Is the encoder's clock stable and synchronized?
*   **Metrics**: PCR Jitter (AC/OJ), PCR Accuracy, PCR Repetition, Clock Drift (ppm).
*   **Primary Goal**: Detecting clock crystal aging, Software PLL instability, or severe network-induced jitter.

### Layer 4: Bitrate & Multiplex Dynamics
*   **Question**: How is bandwidth distributed across programs?
*   **Metrics**: Instantaneous Bitrate, StatMux allocation, Null Packet displacement, PID share.
*   **Primary Goal**: Monitoring VBR efficiency and identifying bandwidth oversubscription.

### Layer 5: Decoder Buffer Safety
*   **Question**: Will the decoder run out of data or crash?
*   **Metrics**: T-STD Buffer Occupancy, Buffer Safety Margin (BSM %), Remaining Safe Time (RST).
*   **Primary Goal**: Predictive alerting before visual corruption occurs.

### Layer 6: Viewer Experience Risk (QoE)
*   **Question**: Will human viewers notice a problem?
*   **Metric**: **QoE Risk Score** (0-100).
    *   **90-100**: Healthy (Nominal).
    *   **70-90**: Warning (Slight impairment).
    *   **50-70**: Degrading (Noticeable artifacts).
    *   **< 50**: Critical (High viewer impact).
*   **Primary Goal**: Quantifying technical debt into human-centric risk.

### Layer 7: Root Cause Inference (Intelligence)
*   **Question**: What component is responsible for the failure?
*   **Logic**: Correlation of lower-layer events (e.g., CC Error + SRT RTT Spike → **NETWORK FAULT**).
*   **Primary Goal**: Instant attribution to either the Network, Encoder, or Multiplexer.

---

## 2. NOC Dashboard Architecture

To implement this model, the TsAnalyzer Pro UI is divided into three functional surfaces:

1.  **The Global Mosaic (Wall)**: A bird's-eye view of all 1000+ streams, color-coded by their Layer 6/7 status.
2.  **The Stream Focus (Diagnostic)**: A 7-Tier grid providing a drill-down into every layer's specific metrics.
3.  **The Forensic Trail (Timeline)**: A synchronized graph showing how an event at Layer 1 (Loss) propagates up to Layer 5 (Underflow) over time.

---

## 3. Alert Prioritization

The appliance avoids "Alarm Storms" by mapping severity to specific layers:

| Severity | Layer Trigger | Example Fault |
| :--- | :--- | :--- |
| **Critical** | Layer 1, 2, 5 | Sync Loss, Missing PAT, Buffer Underflow. |
| **Major** | Layer 3 | PCR Jitter > 500ns, Clock Drift > 50ppm. |
| **Minor** | Layer 4 | StatMux Instability, Bitrate Variance peaks. |

---

## 5. Professional NOC Implementation Specification

To ensure metrological consistency across all monitoring surfaces, the following technical standards are mandated:

### 5.1 Global API & Metric Standards
*   **Standardized Port**: All telemetry is exported via TCP port **8088**.
*   **Metric Namespaces**:
    *   `tsa_system_*`: Core appliance status (Health Score, Signal Lock).
    *   `tsa_metrology_*`: Precision clock and bitrate measurements.
    *   `tsa_compliance_*`: ETR 290 and standard violation counters.
*   **Protocol Compliance**: HTTP responses must include `Content-Type: text/plain; version=0.0.4`.

### 5.2 Visual Layout Standards (Mosaic-First Design)

#### TIER 0: GLOBAL FLEET MOSAIC (WALL)
The primary entry point for NOC operators. It must be implemented as a compact color matrix.
*   **Visual Target**: **1x8 or 8xN tight grid**.
*   **Component**: `stat` panel.
*   **Key Options**:
    *   `Orientation`: Horizontal.
    *   `Text Mode`: Name (Stream ID only).
    *   `Color Mode`: Background (Solid fill based on thresholds).
    *   `RedOptions`: `Values: False`, `Calcs: ["lastNotNull"]`.

#### TIER 1-7: FOCUSED INSIGHT PLANE
Detailed diagnostic panels linked via the `$stream` variable.
*   **L1/L2 (Transport)**: Gauge or Stat panels showing real-time CC/PAT/PMT violations.
*   **L3 (Timing)**: Time-series graphs for PCR Jitter (ms) and Frequency Drift (ppm).
*   **L4 (Dynamics)**: Throughput graphs comparing Physical vs. Content bitrates.
*   **L5 (Buffer)**: Predictive RST (Remaining Safe Time) status.
*   **L6 (Content)**: Video vital stats (FPS, GOP structure).
*   **L7 (RCA)**: Intelligent text-based correlation.

### 5.3 ASCII Layout Representation
```text
+-----------------------------------------------------------------------+
|  [ ST-1 ]  [ ST-2 ]  [ ST-3 ]  [ ST-4 ]  [ ST-5 ]  [ ST-6 ]  [ ST-7 ]  |
+-----------------------------------------------------------------------+
| TIER 1: Master Health (Gauge)  |  TIER 3: ETR 290 P1 (Error List)     |
+--------------------------------+--------------------------------------+
| TIER 4: PCR Jitter Graph (~~)  |  TIER 5: PID Bitrate Revenue         |
+--------------------------------+--------------------------------------+
| TIER 6: Content Vitals (FPS)   |  TIER 7: ALARM EVENT LOG (Table)     |
+--------------------------------+--------------------------------------+
```

