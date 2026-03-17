# Track 025: Architecture Realignment

## Objective
To eliminate technical debt identified during the documentation-to-code alignment review and strictly align the runtime implementation with the "Three-Plane Architecture" and "5-Layer Observation Model".

## Status
- [ ] Phase 1: Threading Model Deep Decoupling
- [ ] Phase 2: QoE Modularization
- [ ] Phase 3: Validation & Benchmarking

## Plan Details

### Phase 1: Threading Model Deep Decoupling
* **Current Issue:** The `io_thread` in `src/tsa_server_pro.c` currently handles socket polling, high-precision timestamping (`ts_now_ns128`), TS framing, and pseudo O(1) synchronous packet forwarding. This violates the multi-thread hierarchy defined in `technical_specification.md`.
* **Action Item 1: Separate `io_thread` and `ingress_thread`**:
  * Modify `io_thread` to focus purely on network polling (epoll/recv). It will push "burst buffers" (raw network blocks) into an intermediate lock-free ring buffer.
  * Create `ingress_thread` which consumes burst buffers, slices them into 188-byte frames, applies high-precision hardware/software timestamps, and feeds the worker queues.
* **Action Item 2: Create Egress/Pacer Thread**:
  * Remove synchronous forwarding from `io_thread`. Implement an independent Egress thread that reads from `tx_q` and uses `clock_nanosleep` for absolute-time CBR pacing.

### Phase 2: QoE Modularization (Layer 3)
* **Current Issue:** The Layer 3 Quality of Experience logic (Entropy calculation, Freeze/Black detection) is currently entangled within the elementary stream handler `src/tsa_es.c`.
* **Action Item 1: Create `tsa_engine_qoe.c`**:
  * Extract the entropy calculation and freeze state logic.
  * Implement as a standardized `tsa_plugin_ops_t`.
* **Action Item 2: Metrics Alignment**:
  * Rename Prometheus metrics in `src/tsa_exporter_prom.c` from `tsa_essence_entropy_freeze_total` to `tsa_qoe_freeze_events_total` to comply with the namespace requirements in Section 10 of the spec.

### Phase 3: Validation & Benchmarking
* Execute the high-density stress scripts to ensure the new cross-thread queues do not degrade L2/L3 cache hit ratios on 16+ core systems.
* Validate QoE metrics correctly fire using static synthetic streams.