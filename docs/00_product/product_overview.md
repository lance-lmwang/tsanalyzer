# TsAnalyzer Product Ecosystem

TsAnalyzer is a tiered ecosystem of software-defined measurement and delivery tools designed for the modern broadcast and OTT landscape.

## 1. Product Line Definition

| Product | Target User | Role | Competitive Benchmark |
| :--- | :--- | :--- | :--- |
| **TsAnalyzer Engine** | Developers / SI | Extreme Performance Metrology Probe | TSDuck, ffprobe |
| **TsAnalyzer Appliance** | NOC Operators | High-Density Monitoring Platform | BridgeTech VB330, Sencore |
| **Smart Assurance Gateway**| Network Engineers | Inline Signal Repair & Fail-safe Relay | Zixi, OpenSrtHub |

---

## 2. Positioning & Vision

### 2.1 TsAnalyzer Engine (The Brain)
A bit-exact, deterministic C library and CLI tool. It treats Transport Streams as physical entities, providing nanosecond-level clock reconstruction and strict ETSI TR 101 290 compliance. It is designed to be embedded into larger broadcast systems.

### 2.2 TsAnalyzer Appliance (The Surface)
A multi-channel server architecture that aggregates metrology from multiple Engines. It provides situational awareness through a 7-tier NOC dashboard, long-term SLA tracking, and Root Cause Analysis (RCA) inference.

### 2.3 Smart Assurance Gateway (The Shield)
An inline processing node that monitors signal health in real-time and takes active measures (Pacing, Shaping, or Fail-safe Bypass) to preserve service continuity across unpredictable IP networks.

---

## 3. Core Philosophical Pillars
1.  **Deterministic Measurement**: Identical input must yield bit-identical analytical results.
2.  **Predictive Telemetry**: Use Buffer Safety Margins and Remaining Safe Time (RST) to alert *before* viewer impact.
3.  **Causal Explainability**: Every fault must be attributable to either the Network or the Encoder via quantifiable scoring.

---

## 4. Performance Targets (Scalability Model)

| Total Streams | Average Bitrate | Aggregate Throughput | Target Hardware |
| :--- | :--- | :--- | :--- |
| **128** | 8 Mbps | 1 Gbps | 8 Core / 16GB |
| **256** | 8 Mbps | 2 Gbps | 16 Core / 32GB |
| **512** | 8 Mbps | 4 Gbps | 32 Core / 64GB |
| **1000** | 8 Mbps | 8 Gbps | 64 Core / 128GB |

---

## 5. Industrial Verification Gates
*   **G1 (Throughput)**: 1.0 Gbps - 10.0 Gbps aggregate throughput with zero kernel drops.
*   **G2 (Metrology)**: 100% TR 101 290 P1/P2 coverage; ±10ns PCR jitter precision.
*   **G3 (Determinism)**: MD5-consistent JSON output for identical PCAP input.
*   **G4 (Stability)**: 24h stability with zero memory growth (RSS flat-line).
