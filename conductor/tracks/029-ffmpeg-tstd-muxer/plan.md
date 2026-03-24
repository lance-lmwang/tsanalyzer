# Implementation Plan: LibTSMux & FFmpeg Integration

## Phase 1: Design & Documentation (Current)
- [x] **Draft Design**: Write detailed design to `conductor/tracks/029-ffmpeg-tstd-muxer/ts_shaper_design.md`.
- [ ] **Publish Design**: Move `ts_shaper_design.md` to `docs/ts_shaper_design.md` (Requires exiting Plan Mode).
- [ ] **Review**: User review of the final document in `docs/`.

## Phase 2: `libtsmux` Implementation
- [ ] **Scaffold**: Create `src/libtsmux/` with build configuration.
- [ ] **Core Modules**:
    -   **RingBuffer**: Implement `SPSCRingBuffer`.
    -   **Pacer Thread**: Implement the high-precision loop with busy-wait.
    -   **Scheduler**: Implement the priority queue logic (PCR > Audio > Video).
    -   **Output**: Implement UDP/RTP/SRT encapsulation logic.
- [ ] **PCR Logic**: Implement nanosecond-to-PCR math and bitwise rewriting.
- [ ] **T-STD Simulator**: Implement basic leaky bucket checks.

## Phase 3: FFmpeg Integration
- [ ] **Patch FFmpeg**:
    -   Modify `libavformat/mpegtsenc.c`.
    -   Add CLI options: `-tsa_shaper 1`, `-tsa_pcr_pid`, `-tsa_output_mode`.
    -   Replace `avio_write` with `tsa_shaper_push`.
- [ ] **Build System**: Link FFmpeg against `libtsmux.a`.

## Phase 4: Validation
- [ ] **Functional Test**: Verify UDP/RTP output using VLC/ffplay.
- [ ] **Precision Test**: Use `tsanalyzer` to measure PCR jitter (Target: < 50ns).
- [ ] **Compliance Test**: Verify T-STD buffer levels under stress.
- [ ] **Performance Test**: Benchmark throughput (Target: > 5Gbps).
