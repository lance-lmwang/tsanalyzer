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
- [ ] Implement the leaky bucket algorithm for TB and MB stages.
- [ ] Map DTS (Decoding Time Stamp) to EB removal events.
- [ ] Calculate real-time `buffer_fullness_pct`.

## Phase 4: Full API & Verification (The Delivery)
- [ ] Update `/api/v1/metrology/full` to include the `buffer_status` and `video_metadata` objects.
- [ ] Create automated tests using the provided `cctvhd.ts` sample.
- [ ] Verify T-STD accuracy against existing MTS4000 logs.
