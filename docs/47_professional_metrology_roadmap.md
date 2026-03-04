# Professional Metrology Roadmap for tsanalyzer

## 1. Overview
This document outlines the strategic roadmap for absorbing the advanced features and methodologies of `Professional PCBR` into `tsanalyzer`. While `tsanalyzer` excels in deterministic, ultra-high-speed transport stream processing and high-precision metrology, `Professional PCBR` provides deep content-layer insights and versatile protocol support that are crucial for comprehensive "Professional QoE" analysis.

## 2. Key Advantages of Professional PCBR
1. **Accurate PCR Accuracy (PCR_AC) via Piecewise Bitrate (`CCalcPcrN1`)**: `Professional PCBR` strictly follows the mathematical definition of PCR_AC by dynamically calculating the transport rate between individual PCR points based on payload packet distance. This is highly accurate for file-based analysis or when system timestamps (VSTC) are noisy.
2. **Deep Content Metrology (PES & GOP Decoding)**: Extracts detailed video parameters, I/P/B frame sequences, and GOP sizes from PES payloads. This directly aligns with `tsanalyzer`'s future goals for QoE and decoding health metrics.
3. **HLS and HTTP Integration**: Native `libhlsanalysis` provides comprehensive M3U8 parsing, chunk downloading, and individual TS segment quality detection.
4. **Third-Party Integrations**: Wraps `ffprobe` and `mediainfo` outputs directly into its structural JSON reports, providing a unified view of both transport health and container/codec metadata.

## 3. Absorption Strategy

### Phase 1: Metrology Enhancements (Short-Term)
- **Implement Strict TR 101 290 PCR_AC**: Integrate the `CCalcPcrN1` logic into `tsa_metrology_process`. Calculate `pcr_expected` based on the exact byte distance between PCRs and the calculated piecewise constant bitrate, running in parallel with the current VSTC linear regression model.
- **Goal**: Provide a highly accurate, deterministic `pcr_accuracy_ns` metric that works perfectly even in offline/file analysis where system clocks don't exist.

### Phase 2: Content-Layer Parsing (Mid-Term)
- **PES Header Parsing (`tsa_pes_parse`)**: Extend the TS demuxer to fully reconstruct PES headers (PTS/DTS).
- **GOP Extraction**: Implement a fast, zero-copy scanner inside the video PES payloads to detect NAL units (H.264/HEVC) and log GOP structures and frame intervals.
- **Integration**: Add these metrics to the `tsa_snapshot_t` to be exported via Prometheus and Grafana.

### Phase 3: Protocol Expansion (Long-Term)
- **HLS Ingest Engine (`tsg_hls.c`)**: Port the curl-based download loop from `Professional PCBR`, but adapt it to `tsanalyzer`'s lock-free `spsc_queue`. Each segment will be fed into the `tsa` engine as if it were a continuous stream, with segment boundaries marked as events.
- **Sidecar FFprobe Integration**: Execute a non-blocking `ffprobe` process alongside `tsanalyzer` for deep codec metadata extraction.

## 4. Architectural Constraints
- **Zero-Copy Rule**: All absorbed PES/GOP parsing logic must operate directly on the ring buffer memory without allocations.
- **O(1) Complexity**: Frame scanning must be highly optimized, avoiding stateful backtracking.
