# Implementation Plan: ES & T-STD Extension

## Phase 1: PES & NALU Foundation (The Scanner)
- [x] Implement a wait-free PES reassembler in `src/tsa.c`.
- [x] Integrate a lightweight NALU start-code scanner (`0x000001`).
- [x] Add `es_type` identification for H.264/H.265.

## Phase 2: Content Metadata Extraction (The Brain)
- [x] Implement H.264 SPS/PPS parsing logic.
- [x] Add GOP structure tracking (detecting I, P, B frame sequences).
- [x] Export `width`, `height`, `fps`, and `gop_n` to the JSON API.

## Phase 3: T-STD Buffer Simulator (The Muscle)
- [x] Implement the leaky bucket algorithm for TB and MB stages.
- [x] Map DTS (Decoding Time Stamp) to EB removal events.
- [x] Calculate real-time `buffer_fullness_pct`.

## Phase 4: Full API & Verification (The Delivery)
- [x] Update `/api/v1/metrology/full`: Include nested `buffer_status` and `video_metadata` objects in the JSON output.
- [/] Create automated tests: `tests/test_tstd_btvhd.c` created, needs logic refinement for large JSON blobs.
- [ ] Verify T-STD accuracy: Compare results against existing MTS4000 logs.
