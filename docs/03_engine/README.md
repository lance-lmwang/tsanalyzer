# Engine Documentation - Native FFmpeg T-STD

This directory contains the authoritative documentation for the native FFmpeg T-STD implementation (located in `libavformat/tstd.c`).

## Native T-STD Core
- [tstd_implementation_plan.md](./tstd_implementation_plan.md): Detailed implementation roadmap and verification strategy.
- [tstd_architecture_specification.md](./tstd_architecture_specification.md): T-STD Multiplexer Architecture Specification.
- [native_tstd_integration.md](./native_tstd_integration.md): High-level integration and runtime state machine.
- [timestamp_discontinuity_design.md](./timestamp_discontinuity_design.md): Authoritative design for discontinuity handling and PCR injection.

## Engine Subsystems
- [ingestion_engine.md](./ingestion_engine.md): Stream ingestion and pre-processing.
- [metrology_core.md](./metrology_core.md): Real-time analysis metrics.
- [tr101290_engine.md](./tr101290_engine.md): TR 101 290 compliance monitoring logic.
- [ring_buffer_deduction.md](./ring_buffer_deduction.md): Buffer management theory.
- [simd_parser_design.md](./simd_parser_design.md): SIMD-optimized parsing details.
- [pipeline_architecture.md](./pipeline_architecture.md): Global stream pipeline topology.
- [structural_decoder.md](./structural_decoder.md): Decoupled decoding path analysis.

## Archive (Legacy/Deprecated)
- [archive/](./archive/): Documents related to deprecated `libtsshaper` or pre-2026 architectural iterations.
