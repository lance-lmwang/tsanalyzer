# TsAnalyzer: Software-Defined Broadcast Metrology

TsAnalyzer is a tiered ecosystem of deterministic, high-performance measurement and delivery tools for Transport Streams. It combines laboratory-grade precision with industrial-scale concurrency.

## 🚀 The Product Line

1.  **TsAnalyzer Engine**: Extreme-performance C probe for developers and system integrators.
2.  **TsAnalyzer Appliance**: Multi-channel monitoring platform with 7-tier NOC visualization and SLA tracking.
3.  **Smart Assurance Gateway**: Inline signal processing node with high-precision pacing and fail-safe bypass.

---

## 📖 Technical Documentation (v3 Architecture)

### 🏛️ Pillar 1: Strategy & Architecture
*   **[Product Overview](./docs/00_product/product_overview.md)**: Positioning, vision, and core pillars.
*   **[System Architecture](./docs/01_architecture/system_architecture.md)**: High-performance data flow and NUMA-local threading.

### 🧬 Pillar 2: Metrology & Math (The Bible)
*   **[TR 101 290 Standard](./docs/02_metrology/tr101290.md)**: Signal health levels and compliance implementation.
*   **[PCR Math & PLL](./docs/02_metrology/pcr_math.md)**: 27MHz clock reconstruction and 3D jitter decomposition.
*   **[PCR Root Cause Engine](./docs/02_metrology/pcr_root_cause.md)**: Distinguishing Encoder vs. Network jitter.
*   **[GOP Structure Analyzer](./docs/02_metrology/gop_analysis.md)**: Content-layer GOP stability and IDR auditing.
*   **[Entropy Analyzer](./docs/02_metrology/entropy_analysis.md)**: Information density and freeze/black frame detection.
*   **[HLS/OTT Model](./docs/02_metrology/hls_analysis_model.md)**: Manifest auditing and fragment download metrology.
*   **[RST+ & Safety Models](./docs/02_metrology/rst_model.md)**: Predictive telemetry and buffer safety margins.

### 🧠 Pillar 3: Analytics & Intelligence
*   **[Alarm & Incident Engine](./docs/04_operations/alarm_engine.md)**: FSM lifecycles, suppression, and incident merging.
*   **[RCA Scoring Model](./docs/04_operations/rca_model.md)**: Explainable fault attribution (Network vs. Encoder).
*   **[SLA Engine](./docs/04_operations/sla_engine.md)**: Long-term availability and compliance grading.
*   **[Forensic Time Machine](./docs/04_operations/forensic_time_machine.md)**: Post-mortem incident rewind and reconstruction.

### 🎛️ Pillar 4: Implementation & Operations
*   **[Ingestion Engine](./docs/03_engine/ingestion_engine.md)**: Hardware timestamping and zero-copy data plane.
*   **[Metrology Core](./docs/03_engine/metrology_core.md)**: Numerical stability, __int128, and sampling barriers.
*   **[Smart Gateway](./docs/05_gateway/smart_gateway.md)**: Pacing engines, XDP bypass, and the smart action matrix.

### 🔌 Pillar 5: Interfaces & Validation
*   **[REST API](./docs/06_interfaces/rest_api.md)** | **[Prometheus](./docs/06_interfaces/prometheus.md)** | **[CLI (tsa_top)](./docs/06_interfaces/cli.md)**
*   **[HTML Forensic Reports](./docs/06_interfaces/forensic_reports.md)**: Portable, interactive incident audit trails.
*   **[Performance Targets](./docs/07_validation/performance_targets.md)**: Success gates and engineering verification.

---

## 💻 Quick Start & Usage

### 1. Standalone Probe (CLI)
```bash
./build/tsa_cli --mode live --srt-url srt://:9000
```

### 2. Full-Stack NOC (Docker Compose)
```bash
docker-compose up -d
```
Visit `http://localhost:3000` for the NOC visualization.

---

## ⚖️ Determinism & Metrology Contract
> *Input (Packet Sequence + HW Timestamp) + Engine version (MD5) = Bit-identical JSON Analysis.*

TsAnalyzer uses a **27MHz Software PLL** for professional jitter analysis, ensuring total immunity from system clock drift.
