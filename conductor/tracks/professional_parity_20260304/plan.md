# Implementation Plan: Professional Parity & Professional Robustness (20260304)

## Phase 1: Section Filter & Assembly Engine (PSI/SI)
**Objective:** Replace naive single-packet table parsing with robust, multi-packet section assembly and CRC validation.
- [x] **Step 1:** Define `ts_section_filter_t` state structure in `tsa_internal.h` to hold fragmented payload buffers, `table_id`, `version_number`, and `section_length`.
- [x] **Step 2:** Implement `tsa_crc32_check` utility function matching ISO/IEC 13818-1 Appendix B.
- [x] **Step 3:** Refactor `process_pat`, `process_pmt`, and `process_sdt` to use the new Section Filter. Accumulate bytes using the TS packet `pointer_field` and handle `pusi` continuity across fragmented sections.
- [x] **Step 4:** Add validation checks: Only commit table updates (e.g., updating `h->pid_stream_type`) if the CRC32 is valid and the `version_number` differs from the active one.

## Phase 2: TR 101 290 State Machines
**Objective:** Evolve from simple counters to industrial-grade alarm latches with specific timeout tracking.
- [x] **Step 1:** Expand `tsa_tr101290_stats_t` to include discrete alarm states (e.g., `bool alarm_pat_error`, `bool alarm_pcr_rep_error`).
- [x] **Step 2:** Implement Time-to-Live (TTL) tracking arrays: `uint64_t pid_last_seen_vstc[TS_PID_MAX]` updated inside `tsa_decode_packet`.
- [x] **Step 3:** Inside the metrology loop (`tsa_commit_snapshot`), calculate $\Delta t$ using the VSTC for PAT, PMT, and SDT. Latch a `TSA_STATUS_DEGRADED` error if:
  - PAT `dt` > 500ms
  - PMT `dt` > 500ms
  - SDT `dt` > 2000ms
- [x] **Step 4:** Implement "Unreferenced PID" detection. During snapshot commit, scan active PIDs against the PMT mapping tables. Flag any PID with bandwidth but no PMT mapping as a Ghost PID.

## Phase 3: Deep PES & ES Inspection
**Objective:** Extract true presentation timing and frame semantics to replace heuristic guessing.
- [x] **Step 1:** Enhance PES header parsing in `tsa_metrology_process`. Safely extract 33-bit `PTS` and `DTS` values, handling extension flags properly.
- [x] **Step 2:** Implement an ES NALU scanner for H.264/HEVC inside the PES payload buffer to accurately count `I/P/B` frames and compute true GOP length (distance between IDR frames).
- [x] **Step 3:** Update the dashboard output to surface the exact Audio/Video Sync drift based on the computed $\Delta(PTS_{video} - PTS_{audio})$ instead of raw arrival differences.

## Phase 4: Validation & Integration
**Objective:** Prove parity with `Professional` using complex test streams.
- [x] **Step 1:** Develop a unit test `test_fragmented_psi.c` that intentionally splits a PAT/PMT across 3 TS packets to verify the Section Assembler.
- [x] **Step 2:** Inject artificial CRC errors in a test stream and verify that the engine ignores the corrupted table update.
- [x] **Step 3:** Run a final regression using `make tsa_file_report` on a standard MPTS file and verify the dashboard successfully logs all TR 101 290 timeouts and AV sync metrics.
