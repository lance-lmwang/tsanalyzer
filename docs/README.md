# TsAnalyzer Documentation Center

Welcome to the official documentation for TsAnalyzer Pro, a carrier-grade Transport Stream Metrology Gateway.

## 1. High-Level Architecture
- **[Technical Specification](./01_architecture/technical_specification.md)**: The "Constitution" of the project. Defines the 5-layer observation model, threading hierarchy, and memory architecture.
- **[Configuration Reference](./01_architecture/configuration_reference.md)**: Detailed manual for all configuration blocks (`metrology`, `compliance`, `essence`, `qoe`, `pipeline`).

## 2. In-Depth Technical Deduction
- **[Clock Domains & Sync](./02_metrology/clock_model.md)**: Definition of the four reference timelines (Wall, Rx, Logic, Media) and isolation principles.
- **[Bitrate Measurement Standards](./02_metrology/bitrate_measurement.md)**: Industrial definitions for Physical (Layer 2) vs Business (Layer 3) throughput and MPTS aggregation.
- **[Metrology & Clock Recovery](./02_metrology/pcr_clock_recovery.md)**: Mathematical derivation of the 27MHz Software PLL and 3D jitter decomposition.
- **[Compliance Engine](./03_engine/tr101290_engine.md)**: $O(1)$ event-driven model for TR 101 290 monitoring and time-wheel scheduling.
- **[Data Plane Optimization](./03_engine/v3_ring_buffer_deduction.md)**: Memory barrier and cache-line isolation strategy for 10Gbps+ links.

## 3. Operations & Observability
- **[Functional Capability Matrix](./functional_capability_matrix.md)**: Current implementation status and product roadmap.
- **[Observability Model](./04_operations/observability_model.md)**: Guide to Prometheus metrics, thresholds, and root-cause diagnosis.

## 4. Interfaces
- **[CLI Reference](./06_interfaces/cli.md)**: Command-line arguments and TUI (`tsa_top`) guide.
- **[REST API Guide](./06_interfaces/rest_api.md)**: Dynamic stream management and snapshot querying.
