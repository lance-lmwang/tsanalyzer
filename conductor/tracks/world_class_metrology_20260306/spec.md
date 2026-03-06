# Track: World-Class Metrology Engine Implementation

## Objective
Elevate TsAnalyzer from a basic packet-counter to a **World-Class, Software-Defined Measurement Instrument**. This track implements the advanced mathematical and architectural designs outlined in `docs/50_world_class_analysis_engine_design.md`, heavily inspired by industry-leading deep-inspection tools.

## Key Requirements

1. **Metrology Core Architecture**
   - Decouple deep mathematical calculations into a dedicated `src/metrology/` directory to keep the main processing pipeline clean and maintainable.

2. **Physical & Transport Telemetry**
   - **IAT (Inter-Arrival Time) Histograms**: Profile network micro-bursts and jitter using packet arrival time deltas, exposing exactly how packets are distributed (e.g., 1ms vs 10ms intervals).
   - **MDI (RFC 4445)**: Delay Factor (DF) and Media Loss Rate (MLR) derived from physical/NIC timestamps.

3. **Protocol Compliance & Clock Analytics**
   - **PCR Walltime Drift (Linear Trend)**: Implement a highly stable linear regression model (Linear Trend) over a sliding window of PCRs to detect long-term encoder clock drift (ppm/ms) against physical system time.

4. **Content Timing & Deep Inspection (Zero-Copy)**
   - **NALU/PES Sparse Sniffer**: Inspect H.264/H.265 payloads directly within the lock-free ring buffer (no allocations). Extract I/P/B frame structures and calculate GOP sizes.
   - **PTS/DTS Alignment (Lip-Sync)**: Track the differential offset between multiple essence streams.
   - **SCTE-35 PTS Audit**: Measure the absolute nanosecond offset between a SCTE-35 Splice command's `pts_time` and the actual PTS of the corresponding I-Frame in the video track.

5. **Forensics & Mitigation**
   - **Triggered Micro-Capture**: Maintain a rolling 500ms lock-free ring buffer of the stream. Upon critical TR 101 290 P1 failure, instantly freeze and dump the "crime scene" to disk for Wireshark analysis.

## Success Criteria
- [x] `pcr_walltime_drift` is mathematically stable and accurately reflects clock drift.
- [x] IAT Histograms are actively tracking packet micro-bursts at 1Gbps line rate.
- [x] The engine correctly identifies GOP boundaries and frame types via the NALU sniffer without full decoding.
- [x] Micro-capture successfully dumps a `.ts` file capturing the exact moment of an injected P1 error.
- [x] Bitrate Smoother eliminates system timer jitter and outputs a perfect CBR stream using `clock_nanosleep`.
