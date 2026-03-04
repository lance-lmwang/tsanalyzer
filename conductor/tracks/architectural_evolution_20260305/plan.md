# Implementation Plan: Architectural Evolution & Advanced Analytics

## Phase 1: Architectural Decoupling (Modular Inspectors)
- [ ] **Extract PSI Engine**: Move `tsa_section_filter_push`, `parse_pat`, `parse_pmt`, and `parse_sdt` into a new file `src/tsa_psi.c`.
- [ ] **Extract Video/Audio Scanners**: Move `tsa_handle_es_payload`, `tsa_handle_video_frame`, and `parse_h264_sps` into `src/tsa_es.c`.
- [ ] **Update Header & Build**: Update `include/tsa_internal.h` and `CMakeLists.txt` to compile and link the new modular files.

## Phase 2: Advanced Timing & Drift Analytics
- [ ] **Extend Metrology State**: Add `stc_wall_drift_ppm` to `tsa_predictive_stats_t` and `tsa_handle_t`.
- [ ] **Drift Calculation Logic**: In `tsa_commit_snapshot`, correlate `stc_ns` (PCR domain) with the actual wall-clock elapsed time (via `gettimeofday` or `CLOCK_MONOTONIC`).
- [ ] **Export Metric**: Add the drift metric to JSON (`final_metrology.json`) and Prometheus (`src/tsa_exporter_prom.c`).

## Phase 3: Broadcast Events & SCTE-35
- [ ] **SCTE-35 Recognition**: Update `tsa_get_pid_type_name` and the PMT parser to recognize Stream Type `0x86` (SCTE-35).
- [ ] **Section Filter Update**: Allow the Section Filter to process Table ID `0xFC`.
- [ ] **Splice Command Parser**: Implement a lightweight parser to extract `splice_command_type` (e.g., `0x05` for Splice Insert) and log it via the CLI/Dashboard.

## Phase 4: Event-Driven Alarms (TR 101 290)
- [ ] **Create Event Queue**: Define a lightweight, lock-free event queue (similar to `opensrthub`'s dataqueue) to hold transient alarm states.
- [ ] **Refactor Alarm Generation**: Modify `tsa_metrology_process` to push distinct alarm events (e.g., `ALARM_CC_ERROR_PID_0x0100`) rather than just incrementing global counters.
- [ ] **Dashboard Integration**: Update `tsa_render_dashboard` to consume and display these distinct events.

## Phase 5: Verification & Testing
- [ ] **Test Case: Modularity**: Ensure `make test` passes without regression after the codebase split.
- [ ] **Test Case: Clock Drift**: Simulate a stream with an artificially fast PCR clock and verify `stc_wall_drift_ppm` accurately reports the divergence.
- [ ] **Test Case: SCTE-35 Parsing**: Create or acquire a TS file with SCTE-35 markers and verify correct detection of Splice Insert commands.