# Implementation Plan: LibTSMux & FFmpeg Integration

## Phase 1: Design & Documentation
- [x] **Draft Design**: Write detailed design to `conductor/tracks/029-ffmpeg-tstd-muxer/ts_shaper_design.md`.
- [x] **Publish Design**: Published to `docs/03_engine/ts_shaper_design.md` (18KB comprehensive spec).
- [x] **Review**: Internal review of the final document in `docs/`.

## Phase 2: `libtsmux` Implementation
- [x] **Scaffold**: Create `src/libtsmux/` (using existing `libtsshaper`).
- [x] **Core Modules**:
    -   **RingBuffer**: `SPSCRingBuffer` (Implemented in `spsc_queue.c`).
    -   **Pacer Thread**: High-precision loop (Implemented in `pacer_loop.c`).
    -   **Scheduler**: Priority queue logic (Enhanced in `interleaver.c`).
    -   **Output**: HAL-based I/O (Implemented in `hal_*.c`).
- [x] **PCR Logic**: Nanosecond-to-PCR math (Implemented in `tstd_model.c`).
- [x] **T-STD Simulator**: Leaky bucket & TB checks (Implemented in `tstd_model.c`).
- [x] **Panic Mode**: Aggressive pacing penalty on TB saturation.
- [x] **Auto-Rate**: PID bitrate estimation for auto-pacing.

## Phase 3: FFmpeg Integration
- [/] **Patch FFmpeg**:
    - [x] Modify `libavformat/mpegtsenc.c`: Added `tsa_shaper` options and logic.
    - [x] Modify `configure`: Added `--enable-libtsshaper` support.
    - [ ] Build System: Link FFmpeg against `libtsshaper.a`.
- [ ] **Build System**: Finalize linkage in FFmpeg's `libavformat/Makefile`.

## Phase 4: Validation
- [x] **Functional Test**: `test_real_file_shaping` verified.
- [x] **Precision Test**: `verify_es_fluctuation.py` confirms CV reduction from 228% to 14%.
- [ ] **Compliance Test**: Verify with Tektronix/TSDuck.
- [ ] **Performance Test**: Benchmark throughput.
