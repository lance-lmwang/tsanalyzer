# Implementation Plan: Alarm Flapping Suppression

## Status
- [ ] **Phase 1: Research & Baseline**
  - [ ] Audit `src/tsa_alert.c` for current `tsa_alert_push` implementation.
  - [ ] Benchmark existing alert dispatch latency as a baseline.
  - [ ] Identify `signal_thread` queueing entry points.

- [ ] **Phase 2: Data Structure Implementation**
  - [ ] Define `tsa_alert_aggregator_t` in `include/tsa_alert.h`.
  - [ ] Implement a pre-allocated hash table for tracking alert states.
  - [ ] Implement atomic operations for `hit_count` and `timestamp` updates.

- [ ] **Phase 3: Core Algorithm Implementation**
  - [ ] Develop `tsa_alert_should_suppress()` logic.
  - [ ] Implement the `aggregation_timer` using the system's existing O(1) Time-Wheel.
  - [ ] Add the summary report generation logic.

- [ ] **Phase 4: Integration & Configuration**
  - [ ] Add `alert_suppression_window` (ms) to `tsa_conf.h`.
  - [ ] Modify `tsa_alert_push()` to route through the aggregator.
  - [ ] Update JSON logging to include `suppressed_count`.

- [ ] **Phase 5: Validation & Testing**
  - [ ] Create a regression test `tests/test_alert_suppression.py`.
  - [ ] Simulate rapid alert flapping (500ms intervals) for 60s.
  - [ ] Verify that only 1 initial alert + 1 summary per window are received.
  - [ ] Measure CPU overhead of the aggregation table under stress.

## Completion Criteria
1.  **Noise Reduction**: 90% reduction in Webhook calls for flapping streams.
2.  **Latency**: Zero additional delay for the FIRST alert occurrence.
3.  **Correctness**: Accurate reporting of `hit_count` in summary alerts.
