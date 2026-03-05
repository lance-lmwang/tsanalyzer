# Specification: Architectural Evolution & Advanced Analytics

Based on a comprehensive review of leading broadcast analyzers, `tsanalyzer` has achieved a high level of accuracy and performance. However, to scale toward a fully modular, enterprise-grade probe, several architectural and analytical optimizations are required. The current `tsa.c` is becoming a monolith, and advanced broadcast features like SCTE-35 and clock drift tracking are missing.

## Key Objectives

1. **Modular Architecture**: Decouple the monolithic `tsa.c` into domain-specific inspectors, improving maintainability and testability.

2. **Advanced Clock Analytics**: Implement System-Clock-to-PCR-Drift (ppm) tracking to identify encoder clock instabilities.
3. **Broadcast Event Detection**: Introduce SCTE-35 (Splice Information Table) parsing to detect ad-insertion points.
4. **Event-Driven Alarms**: Shift the TR 101 290 engine from state-overwriting to an event-driven stream to ensure transient errors are never missed.

## Technical Requirements
- **Decoupling**: Create `src/tsa_video.c`, `src/tsa_audio.c`, and `src/tsa_psi.c` without compromising the zero-copy philosophy.
- **Timing**: Expose `stc_drift_ppm` as a tier-2 predictive metric in the Prometheus exporter and JSON report.
- **SCTE-35**: Implement a lightweight section filter specifically for `table_id == 0xFC`.
- **Zero-Allocation**: All new parsing modules must operate directly on the ring buffer or pre-allocated pools.