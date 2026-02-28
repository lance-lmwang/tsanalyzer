# TsAnalyzer: Deterministic Transport Stream Metrology Platform

TsAnalyzer is a professional-grade **Software-Defined Measurement Instrument** designed for broadcast-grade TS analysis. It combines laboratory-grade protocol depth with a 1.2M PPS high-performance deterministic engine.

---

## 🚀 Phase 1 Engineering Vision

Our mission is to provide the absolute **Source of Truth** for TS metrology, ensuring that analysis results are mathematically precise, bit-exact reproducible, and experimentally verifiable.

### Core Implementation Pillars:
- **Temporal Fidelity**: Sub-microsecond arrival precision via NIC Hardware Timestamping.
- **Deterministic Engine**: 100% bit-identical results across PCAP replays (MD5-consistent JSON).
- **Decoder Digital Twin**: ISO/IEC 13818-1 Annex D faithful buffer simulation (VBV/T-STD).
- **MTTR Reduction**: Slashing fault isolation time from hours to minutes via deterministic diagnostics.

---

## 📚 Technical Documentation Stack (Phase 1)

The following documents define the "Instrument-Grade" specification of TsAnalyzer:

### 1. Strategy & Overview
- **[Product Overview](./docs/00_product_overview.md)**: The V2.3 Whitepaper defining the Engineering Identity and Metrology Domains.
- **[Engine Execution Model](./docs/01_engine_execution_model.md)**: Deterministic runtime architecture, thread topology, and packet ownership rules.
- **[Timing Model](./docs/02_timing_model.md)**: Temporal physics defining HAT, STC, and VSTC domains.
- **[Buffer Model](./docs/03_buffer_model.md)**: Deterministic ISO/IEC 13818-1 Annex D simulation and RST derivation.
- **[Metrology Model](./docs/04_metrology_model.md)**: Measurement theory, causality engine, and traceability contract.
- **[Determinism Contract](./docs/05_determinism_contract.md)**: The "Constitution" guaranteeing bit-identical reproducibility.
- **[Performance Contract](./docs/06_performance_contract.md)**: Throughput and latency budgets as correctness requirements.
- **[Error Model](./docs/07_error_model.md)**: Error propagation physics and measurement validity hierarchy.
- **[Validation Methodology](./docs/08_validation_methodology.md)**: The framework for proving accuracy, repeatability, and real-world equivalence.
- **[Operational Modes](./docs/09_operational_modes.md)**: Trust levels defining measurement authority.
- **[Engine Constraints](./docs/10_engine_constraints.md)**: Non-negotiable architectural laws to preserve metrological integrity.
- **[Implementation Roadmap](./docs/11_implementation_roadmap.md)**: The sequential path to achieving instrument-grade precision.

### 2. Technical Specifications
- **[TR 101 290 Metrology Spec](./docs/16_tr101290_analysis_spec.md)**: Detailed mathematical implementation of P1/P2/P3, V-STC, VBV simulation, and commercial ad-audits.
- **[Deterministic 4-Layer Architecture](./docs/12_system_architecture_diagram.md)**: Low-level engine design including NUMA, Zero-copy, and L2-cache optimizations.
- **[High-Performance Server Design](./docs/34_server_high_performance_design.md)**: Bare-metal OS sealing and math precision preservation strategies.

### 3. Verification & Determinism
- **[Engine Verification Matrix](./docs/21_engine_verification_matrix.md)**: The "Proof of Correctness" protocols for hardware-reference benchmarking.
- **[Determinism Threat Model](./docs/22_determinism_threat_model.md)**: Systematic identification and mitigation of timing and memory entropy.
- **[Resource & Performance Spec](./docs/15_resource_performance.md)**: Defined throughput, latency, and determinism gates (G1-G4).

---

## 🛠️ Key Metrology Metrics
| Tier | Category | Key Indicators |
| :--- | :--- | :--- |
| **Tier 1** | IP/Transport | MDI (DF:MLR), Path Skew (2022-7), SRT RTT. |
| **Tier 2** | MPEG-TS | TR 101 290 P1/P2/P3, **RST (Remaining Safe Time)**. |
| **Tier 3** | Content Audit | SCTE-35 Precision Alignment, Audio Loudness (BS.1770-4). |
| **Tier 4** | ABR Synergy | Cross-profile PTS Drift, IDR/GOP Boundary matching. |

---

## ⚖️ Determinism Contract
> *Input (Packet Sequence + HW Timestamp) + Engine version (MD5) = Bit-identical JSON Analysis.*

This contract ensures that TsAnalyzer is not just a monitoring tool, but a **Forensic Grade Instrument** suitable for legal and SLA dispute resolution.
