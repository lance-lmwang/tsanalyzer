# Implementation Plan: Alarm Flapping Suppression

## Status
- [x] **Phase 1: Research & Baseline**
  - [x] Audit `src/tsa_alert.c` for current `tsa_alert_push` implementation. (Found `tsa_alert_update`)
  - [x] Benchmark existing alert dispatch latency as a baseline.
  - [x] Identify `signal_thread` queueing entry points.

- [x] **Phase 2: Data Structure Implementation**
  - [x] Define `tsa_alert_aggregator_t` in `include/tsa_alert.h`.
  - [x] Implement a pre-allocated hash table for tracking alert states.
  - [x] Implement atomic operations for `hit_count` and `timestamp` updates.

- [x] **Phase 3: Core Algorithm Implementation**
  - [x] Develop `tsa_alert_should_suppress()` logic.
  - [x] Implement the `aggregation_timer` using the system's existing O(1) Time-Wheel. (Integrated into `tsa_alert_check_resolutions`)
  - [x] Add the summary report generation logic.

- [x] **Phase 4: Integration & Configuration**
  - [x] Add `alert_suppression_window` (ms) to `tsa_conf.h`.
  - [x] Modify `tsa_alert_push()` to route through the aggregator.
  - [x] Update JSON logging to include `suppressed_count`.

- [x] **Phase 5: Validation & Testing**
  - [x] Create a regression test `tests/test_alert_suppression.c`.
  - [x] Simulate rapid alert flapping (500ms intervals) for 60s. (Verified with 100ms intervals in test)
  - [x] Verify that only 1 initial alert + 1 summary per window are received.
  - [x] Measure CPU overhead of the aggregation table under stress.

## Completion Criteria
1.  **Noise Reduction**: 90% reduction in Webhook calls for flapping streams. (Verified)
2.  **Latency**: Zero additional delay for the FIRST alert occurrence. (Verified)
3.  **Correctness**: Accurate reporting of `hit_count` in summary alerts. (Verified)
