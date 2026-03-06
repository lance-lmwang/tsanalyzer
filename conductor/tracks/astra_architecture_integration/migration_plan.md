# Implementation Plan: Astra Architecture Integration - Plugin Migration

## Objective
To cleanly decouple monolithic analysis logic from `tsa.c` into independent Stream Tree plugins (TR101290, PCR, Codec, Essence) without breaking existing tests or corrupting the `tsa_handle_t` state.

## Phase 1: Tooling Standardization
- [ ] **Step 1:** Extract `tsa_parse_pes_header` and `extract_pcr` from `tsa.c` to `tsa_utils.c` and declare them in `tsa_internal.h`.
- [ ] **Step 2:** Extract `cc_classify_error` and `tsa_get_pid_type_name` to `tsa_utils.c`.
- [ ] **Step 3:** Move PID tracking tools (`tsa_precompile_pid_labels`, `tsa_reset_pid_stats`, `tsa_update_pid_tracker`) to `tsa_psi.c`.

## Phase 2: Plugin Registration Refactoring
- [ ] **Step 1:** Update `tsa_plugin_register` in `tsa_plugin.c` to reject duplicates and gracefully handle limits.
- [ ] **Step 2:** Expose `extern tsa_plugin_ops_t` for all default plugins in `tsa_plugin.h`.
- [ ] **Step 3:** Refactor `tsa_create()` to correctly initialize `stc_slope_q64 = 1.0` and `last_pcr_ticks = INVALID_PCR`. Register all plugins to the global registry before attaching them to the instance.

## Phase 3: Core Dispatcher Simplification (`tsa.c`)
- [ ] **Step 1:** Remove TR101290 CC logic and PCR regressions from `tsa_metrology_process`.
- [ ] **Step 2:** Ensure `tsa_process_packet` handles basic decode (`tsa_decode_packet`), core state updates (packet counts), and plugin dispatch (`tsa_stream_send`).
- [ ] **Step 3:** Deprecate and remove `tsa_metrology_process` entirely.

## Phase 4: Plugin Logic Implementation
- [ ] **Step 1:** TR101290: Implement CC error checks in `tr_on_ts`. Implement PMT/PAT timeouts in `tr_commit`.
- [ ] **Step 2:** PCR: Implement piecewise accuracy and jitter calculations in `pcr_on_ts`. Implement regression drift logic in `pcr_commit`.
- [ ] **Step 3:** Update `tsa_commit_snapshot` to call `ops->commit()` for all plugins before freezing the snapshot.

## Phase 5: Verification
- [ ] **Step 1:** Refactor `tests/test_tr101290.c` to test the engine via `tsa_process_packet` rather than direct static function calls.
- [ ] **Step 2:** Run full test suite (`ctest -j8`) to ensure 100% pass rate.