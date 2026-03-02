# TsAnalyzer Pro: Documentation Map

This documentation suite defines the architecture, metrology, and operational standards for the TsAnalyzer Pro Appliance.

---

## 🏛️ Pillar 1: Product Architecture (The Vision)
*   **[00: Product Overview](./00_product_overview.md)**: Vision, glossary, and core mission.
*   **[31: Monitoring PRD](./31_monitoring_prd.md)**: Product requirements for alarm lifecycles and forensics.
*   **[45: Server Pro Design](./45_tsa_server_pro_design.md)**: Core side-car architecture and high-performance pipeline.
*   **[12: System Architecture](./12_system_architecture_diagram.md)**: Functional block mapping and decision logic.

## 🧬 Pillar 2: Technical Specifications (The Physics)
*   **Metrology Core**:
    *   **[04: Metrology Model](./04_metrology_model.md)**: Definitions of measurement stability.
    *   **[15: TR 101 290 Metrology](./16_tr101290_analysis_spec.md)**: Broadcast-grade protocol analysis standards.
    *   **[07: Error Model](./07_error_model.md)**: Error propagation and detection physics.
*   **Temporal & Buffer Models**:
    *   **[02: Timing Model](./02_timing_model.md)**: Deterministic hardware timestamping.
    *   **[03: Buffer Model](./03_buffer_model.md)**: Annex D / T-STD simulation logic.
*   **Causal & Analysis Intelligence**:
    *   **[30: Causal Analysis](./30_causal_analysis_spec.md)**: RCA scoring and predictive RST models.
    *   **[18: State History Engine](./18_state_history_engine_spec.md)**: Long-term SLA auditing logic.
    *   **[19: KPI Aggregation](./19_kpi_aggregation_spec.md)**: Fleet-wide metric rollup rules.

## 🎛️ Pillar 3: Monitoring & Appliance Surface (The Interface)
*   **[44: Grafana Dashboard Spec](./44_grafana_dashboard_spec.md)**: Three-Plane Architecture and 4K Mosaic Wall.
*   **[46: Inference Engine Spec](./46_inference_engine_implementation.md)**: PromQL-based "Banner Truth" logic.
*   **[43: Server API Design](./43_server_api_design.md)**: REST/gRPC endpoints and orchestration.
*   **[35: Industrial Monitoring](./35_industrial_monitoring_guide.md)**: Real-world NOC deployment guide.
*   **[33: Professional QoE Design](./33_professional_qoe_design.md)**: Content-layer visual auditing standards.

## 📜 Pillar 4: Performance Contracts & Guardrails (The Guardrails)
*   **[05: Determinism Contract](./05_determinism_contract.md)**: Reproducibility guarantees.
*   **[06: Performance Contract](./06_performance_contract.md)**: Throughput and latency bounds.
*   **[10: Engine Constraints](./10_engine_constraints.md)**: Non-negotiable implementation rules.
*   **[15: Resource Performance](./15_resource_performance.md)**: Hardware density and node throughput.
*   **[22: Determinism Threat Model](./22_determinism_threat_model.md)**: Factors affecting result consistency.

## 🚀 Pillar 5: Engineering & Verification (The Delivery)
*   **[11: Implementation Roadmap](./11_implementation_roadmap.md)**: Phase-by-Phase milestone tracking.
*   **[32: Verification Strategy](./32_verification_strategy.md)**: Stress testing and chaos simulation.
*   **[21: Verification Matrix](./21_engine_verification_matrix.md)**: Phase-by-phase success criteria.
*   **[08: Validation Methodology](./08_validation_methodology.md)**: Measurement verification framework.
*   **[41: Design Review Guide](./41_design_review_guide.md)**: Internal code and architectural standards.
*   **[42: Forensic Verification](./42_forensic_verification_guide.md)**: Scenario-based evidence auditing.
*   **[48: Maintenance Guardrails](./48_troubleshooting_and_guardrails.md)**: CRITICAL redlines to prevent functional regressions and "No Data" issues.
*   **[Production Readiness](./production_readiness_report.md)**: Final deployment gate audit.
