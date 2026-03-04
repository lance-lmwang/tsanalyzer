# Implementation Plan: Professional Content-Layer Analysis (PES & GOP)

## Phase 1: Data Structure Extension
- [ ] **Extend `tsa_handle_t`**: Add buffers for PES assembly state per PID (to handle multi-packet PES).
- [ ] **Extend `ts_decode_result_t`**: Add fields for `has_pes_header`, `pts`, and `dts`.
- [ ] **State Trackers**: Allocate structures for GOP counting and NALU state tracking.

## Phase 2: PES Header Parser Implementation
- [ ] **Implement `tsa_parse_pes_header`**: Extract 33-bit PTS/DTS and header flags.
- [ ] **Implement PTS Re-sampler**: Reconstruct a 64-bit monotonic PTS clock.
- [ ] **A/V Sync Logic**: Implement a drift-tracking function that compares the latest video PTS with the audio PTS of a related PID.

## Phase 3: Fast NALU Scanner & Frame Detection
- [ ] **Implement Start-Code Scanner**: Optimized loop to find H.264/HEVC start codes in payload bytes.
- [ ] **NALU Classifier**: Identify key NALU types (SPS/PPS/SEI/IDR/Non-IDR).
- [ ] **Slice Header Decoder**: Minimal parsing to extract `slice_type` for P vs B frame discrimination.

## Phase 4: Metrology & Metrics Integration
- [ ] **Update `tsa_commit_snapshot`**: Calculate GOP length and GOP duration at setiap I/IDR frame.
- [ ] **Metric Export**: Expose `tsa_video_gop_n` and `tsa_essence_av_sync_ms` in the Prometheus exporter.
- [ ] **Grafana Dashboards**: Update the 'Content Quality' panel to visualize frame distributions.

## Phase 5: Verification & Testing
- [ ] **Test Case: Long Stream Rollover**: Simulate a stream crossing the 33-bit PTS wrap.
- [ ] **Test Case: GOP Accuracy**: Use a known golden stream to verify GOP length extraction.
- [ ] **Test Case: Zero-Copy Performance**: Measure throughput impact with NALU scanning enabled.
