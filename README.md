# TsAnalyzer: Software-Defined Broadcast Metrology

TsAnalyzer is a tiered ecosystem of deterministic, high-performance measurement and delivery tools for Transport Streams. It combines laboratory-grade precision with industrial-scale concurrency.

## The Product Line

1.  **TsAnalyzer Engine**: Extreme-performance C probe for developers and system integrators.
2.  **TsAnalyzer Appliance**: Multi-channel monitoring platform with 7-tier NOC visualization and SLA tracking.
3.  **Smart Assurance Gateway**: Inline signal processing node with high-precision pacing, fail-safe bypass, and T-STD compliant reshaping.

---

## 📚 Technical Library (v3 Architecture)

### 🏗 Pillar 1: Strategy & Architecture
*   **[Product Vision](./docs/00_product/product_overview.md)**: Positioning, core mission, and technical pillars.
*   **[Internal System Architecture](./docs/01_architecture/system_architecture.md)**: Dual-Mode Y-Architecture (Static/Lua) and **HAL (SIMD Dispatching)**.
*   **[Technical Specification](./docs/01_architecture/technical_specification.md)**: Bit-accurate threading and execution flow规约.
*   **[Lua Security & Sandbox](./docs/05_pipeline/lua_security.md)**: Safety hardening for scriptable gateway mode.
*   **[Configuration Reference](./docs/01_architecture/configuration_reference.md)**: Exhaustive parameter dictionary for `tsa.conf`.
*   **[Functional Matrix](./docs/functional_capability_matrix.md)**: Feature status vs. industry competitors.

### 🧪 Pillar 2: Metrology & Math (The Bible)
*   **[TR 101 290 Standard](./docs/02_metrology/tr101290.md)**: Compliance levels and implementation details.
*   **[Determinism & Protocol](./docs/07_validation/determinism_protocol.md)**: Quantitative bit-accuracy guarantees.
*   **[PCR Clock Recovery](./docs/02_metrology/pcr_clock_recovery.md)**: 3D jitter decomposition and Software PLL.
*   **[PCR Root Cause Engine](./docs/02_metrology/pcr_root_cause.md)**: Isolating Network vs. Encoder jitter.
*   **[T-STD Buffer Model](./docs/02_metrology/tstd_buffer_model.md)**: Annex D simulation and **Predictive Underflow**.
*   **[GOP & Content Analysis](./docs/02_metrology/gop_analysis.md)**: IDR auditing and frame-type distribution.
*   **[Entropy & QoE](./docs/02_metrology/entropy_analysis.md)**: Information density for freeze/black detection.
*   **[Deep Math Reference](./docs/02_metrology/clock_model.md)** | **[Bitrate Theory](./docs/02_metrology/bitrate_measurement.md)** | **[Industrial Alignment](./docs/02_metrology/industrial_alignment.md)**

### ⚙️ Pillar 3: Engineering & Ingestion
*   **[Ingestion Engine](./docs/03_engine/ingestion_engine.md)**: Hardware timestamping and zero-copy ingress.
*   **[Pipeline Architecture](./docs/03_engine/pipeline_architecture.md)**: Lock-free SPSC routing logic.
*   **[SIMD Parser Design](./docs/03_engine/simd_parser_design.md)**: AVX-512/AVX2 implementation details.
*   **[Pacing Architecture](./docs/03_engine/tsp_pacing_architecture.md)**: High-precision scheduling math.
*   **[Structural Decoder](./docs/03_engine/structural_decoder.md)** | **[Ring Buffer Theory](./docs/03_engine/v3_ring_buffer_deduction.md)** | **[TR-Engine O(1)](./docs/03_engine/tr101290_engine.md)**

### 🛠 Pillar 4: Operations & Scalability
*   **[Alarm & Incident Engine](./docs/04_operations/alarm_engine.md)**: Stateful FSM and **Alert Suppression Tree**.
*   **[7-Layer Observability](./docs/04_operations/observability_model.md)**: Tiered abstraction model.
*   **[Forensic Time Machine](./docs/04_operations/forensic_time_machine.md)**: Post-mortem incident reconstruction.
*   **[RCA Scoring](./docs/04_operations/rca_model.md)** | **[SLA Engine](./docs/04_operations/sla_engine.md)** | **[ASCII Monitor Design](./docs/04_operations/ascii_interface_design.md)**

### 🔗 Pillar 5: Interfaces & Integration
*   **[REST API](./docs/06_interfaces/rest_api.md)** | **[Prometheus](./docs/06_interfaces/prometheus.md)** | **[CLI (tsa_top)](./docs/06_interfaces/cli.md)**
*   **[Native SDK Guide](./docs/06_interfaces/native_sdk_guide.md)** | **[Prometheus UI Alignment](./docs/06_interfaces/prometheus_ui_alignment.md)**
*   **[HTML Forensic Reports](./docs/06_interfaces/forensic_reports.md)**: Interactive incident audit trails.

---

## ⚡ Quick Start

### 1. Standalone Analyzer (CLI)
```bash
./build/tsa_cli --mode live --srt-url srt://:9000
```

### 2. Dynamic Gateway (Lua Scripting)
Create `gateway.lua`:
```lua
-- Simple Failover & Analysis Topology
local src = tsa.udp_input(5000)
local ana = tsa.analyzer()
local out = tsa.udp_output('10.0.0.1', 6000)

ana:set_upstream(src)
out:set_upstream(ana)

ana:on('SYNC', function(evt)
    tsa.log("CRITICAL: Sync lost on PID " .. evt.pid)
end)
```
Run it:
```bash
./build/tsa_cli run gateway.lua
```

### 3. Full-Stack NOC (Docker Compose)
```bash
docker-compose up -d
```
Visit `http://localhost:3000` for the Grafana-based NOC visualization.

---

## 📖 Reference & Compliance
*   **[Metrology Glossary](./docs/glossary.md)**: Unified dictionary of industry terms.
*   **[Software Bill of Materials (SBOM)](./docs/08_compliance/SBOM.md)**: Third-party licenses and compliance.
*   **[Security Policy](./SECURITY.md)**: Vulnerability disclosure and hardening.

---

## Determinism & Metrology Contract
> *Input (Packet Sequence + HW Timestamp) + Engine version (MD5) = Bit-identical JSON Analysis.*
