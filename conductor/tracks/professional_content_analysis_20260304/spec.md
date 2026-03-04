# Specification: Professional Content-Layer Analysis (PES & GOP)

## 1. Objective
Advance `tsanalyzer` from transport-stream metrology to deep essence-layer quality assessment. This phase focuses on extracting precise timing and structural metadata from PES (Packetized Elementary Stream) payloads without compromising the 1Gbps zero-copy architecture.

## 2. Scope
### 2.1 PES Header & Timing Extraction
- **PTS/DTS Recovery**: Correctly parse the 33-bit timestamps from PES headers.
- **Rollover Handling**: Ensure monotonic time reconstruction across 26.5-hour wrap-arounds.
- **A/V Sync Metrology**: Calculate the real-time offset between audio and video PTS for related programs.

### 2.2 GOP (Group of Pictures) Structural Analysis
- **NAL Unit Scanning**: Implement a fast, stateful scanner to detect H.264/HEVC start codes (`0x000001` / `0x00000001`).
- **Frame Type Detection**: Identify I, P, and B frames based on NAL unit types and slice headers.
- **GOP Metrics**: 
    - **N**: Total frames in GOP.
    - **M**: P-frame interval (B-frame count).
    - **GOP MS**: Duration of the GOP in milliseconds.

### 2.3 Professional Snapshot Integration
- **Essence Metrics**: Populate `tsa_pid_info_t` with:
    - `video_fps` (derived from PTS deltas).
    - `gop_n`, `gop_min`, `gop_max`.
    - `i_frames`, `p_frames`, `b_frames` counters.
- **Threshold Alarming**: Trigger forensic events on GOP anomalies (e.g., GOP > 2.0s).

## 3. Technical Constraints
- **Zero-Copy Constraint**: All NALU scanning must occur directly on the incoming ring buffer bytes.
- **Efficiency**: Scanning should be O(1) per packet, utilizing the `ts_decode_result_t` to skip transport headers.
- **Stateless/Stateful Hybrid**: Use the `tsa_handle` to maintain NALU state across packet boundaries for fragmented payloads.
