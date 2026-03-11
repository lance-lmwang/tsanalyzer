# Implementation Plan: Industrial Supremacy (ltntstools Beyond)

## Status
- [x] **Phase 1: Predictive T-STD Buffer Modeling**
  - [x] Research: Review ISO/IEC 13818-1 T-STD specifications for VBV/CPB math.
  - [x] Implement: Add `tsa_tstd_model_t` to `tsa_es_track.c` for tracking byte ingestion vs PTS decoding.
  - [x] Implement: Add predictive logic to calculate `time_to_underflow_ms` and `time_to_overflow_ms`.
  - [x] Validate: Inject an artificial DTS delay in `test_1m.ts` and assert the predictive alarm fires.

- [x] **Phase 2: TR 101 290 Alert Suppression & Timeout Engine**
  - [x] Implement: Integrate a system-clock-driven "Timer Wheel" or "Tick Thread" for zero-packet timeout evaluation (e.g., PAT > 500ms).
  - [x] Implement: Define `tsa_alert_dependency_t` matrix in `tsa_alert.c` (Sync Loss -> Mute CC/PCR).
  - [x] Refactor: Modify the alert emitter to query the suppression matrix before dispatching an event to the Webhook/UI.
  - [x] Validate: Simulate a hard network drop and verify that only `Loss of Sync` and `PID Drop` are emitted, suppressing secondary table timeouts.

- [x] **Phase 3: Lua-Powered Deep Content Inspection (SCTE-35)**
  - [x] Implement: Build a generic `SectionExtractor` in C that passes `(uint8_t* payload, size_t len)` to `tsa_lua.c`.
  - [x] Lua Script: Write `plugins/scte35_parser.lua` to decode `splice_info_section()`.
  - [x] Integration: Bind the `0x86` PID detection in PMT to automatically route payloads to the Lua SCTE-35 plugin.
  - [x] Validate: Feed a known SCTE-35 stream (or synthetic PCAP) and verify that the splice insert markers are accurately logged in JSON format.

- [x] **Phase 4: Optimization & Release**
  - [x] Profiling: Run `perf` / `valgrind` to ensure the new Lua bridging and T-STD math do not break the "Zero-Allocation" mandate in the main packet loop.
  - [x] Documentation: Update `docs/functional_capability_matrix.md` highlighting the new SCTE-35 and T-STD capabilities against competitors.
