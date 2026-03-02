# Track: Appliance Architecture Implementation

**Goal**: Transform `tsanalyzer` from a standard software server into a COTS Inline Broadcast Assurance Appliance as defined in `docs/45_tsa_server_pro_design.md`.
**Focus**: Zero-allocation hot path, strict Side-Car forwarding (Dual-Queue), deterministic Time-Slice Quota scheduling, and robust state machine resync.

## Phase 1: Analyzer Core Zero-Allocation & Determinism (Independent)
*Status: [x] Completed*
- [x] **Zero-Allocation Hot Path**: Refactor `tsa.c` (`tsa_handle_ts_payload`, `tsa_process_packet`, etc.) to absolutely eliminate dynamic memory allocation (`malloc`/`calloc`/`realloc`) during steady-state analysis.
- [x] **Slab/Pre-allocation**: Modify `tsa_create` to pre-allocate all required buffers (e.g., `pid_pes_buf`) within bounded limits to prevent page fault jitter.
- [x] **State Resync Support**: Implement the `tsa_handle_internal_drop(tsa_handle_t* h, uint64_t drop_count)` function. When the ANA Queue overflows, the analyzer must gracefully reset relevant TR 101 290 expectations (like Continuity Counter) to avoid false-positive alerts.
- [x] **Decoupled PCR Math**: Ensure PCR calculations strictly depend on passed hardware/PHY timestamps, completely independent of OS/Worker execution time.
- [x] **Validation**: Create an independent offline C test (`test_analyzer_zero_alloc.c`) that validates no `malloc` is called post-initialization and tests the `internal_drop` resync logic using simulated TS streams.

## Phase 2: High-Performance Data Pipeline (SPSC & MPSC)
*Status: [x] Completed*
- [x] **SPSC Cache Optimization**: Verify and enforce `alignas(64)` on `spsc_queue.h` `head` and `tail` pointers to ensure total elimination of False Sharing across NUMA boundaries.
- [x] **MPSC Ready Queue Implementation**: Create a bounded lock-free MPSC (Multi-Producer Single-Consumer) queue structure to track active `stream_id`s.
- [x] **Atomic Deduplication**: Implement the `atomic_bool scheduled` flag per stream for Double-Checked Edge-Triggering to prevent MPSC push storms during burst events.
- [x] **Validation**: Write a multi-threaded stress test (`test_mpsc_ready_queue.c`) verifying queue limits and atomic deduplication under 10k stream burst simulations.

## Phase 3: Server Side-Car Forwarding Integration
*Status: [ ] Pending*
- [ ] **Dual-Queue Instantiation**: In `tsa_server_pro.c`, update `conn_t` to hold two SPSC queues per stream: `tx_q` and `ana_q`.
- [ ] **O(1) Primary Forwarding Path**: Implement the Egress thread (or handle directly in I/O if AF_XDP/Socket permits). Ensure packets received are pushed to `tx_q` and forwarded to network/disk with **Zero Modification** and **Emission Granularity Consistency** (preserving bursts).
- [ ] **Best-Effort ANA Enqueue**: After TX push, attempt to push the pointer/packet to `ana_q`. 
- [ ] **Silent Fast-Fail**: If `ana_q` is full, increment `atomic_inc(&c->internal_drop)` and return instantly without logging or blocking the Forward Path.
- [ ] **Edge-Triggered Wakeup**: If `ana_q` transitions from empty to non-empty, use the double-checked relaxed atomic logic to push the `stream_id` to the MPSC Ready Queue.

## Phase 4: Time-Slice Quota & Worker Pool Integration
*Status: [ ] Pending*
- [ ] **TSC Time-Slice Quota**: Replace the `2000` packet limit in `worker_thread` with a strict Time-Slice limit (e.g., 500µs). Use `rdtsc` (Time Stamp Counter) to calculate elapsed execution time with deterministic nanosecond precision.
- [ ] **Worker Pop Logic**: Workers must pop from the MPSC Ready Queue instead of linearly scanning all `g_conn_count`.
- [ ] **State Machine Resync Execution**: When a Worker processes a stream, it first checks if `internal_drop > 0`. If so, it calls `tsa_handle_internal_drop()` and resets the counter to 0 before analyzing the new packets.
- [ ] **Clear Scheduled Flag**: Ensure the Worker clears the `atomic_bool scheduled` flag *only* after its time slice expires.

## Phase 5: System Verification & Telemetry
*Status: [ ] Pending*
- [ ] **Appliance-Grade Telemetry**: Add Promethus metrics for:
    - ANA queue fill ratio (or current size / capacity).
    - `internal_analyzer_drop` counters (explicitly separated from TR 101 290).
    - Worker slice overrun counters (detecting if a time-slice exceeded its 500µs budget due to OS scheduling).
- [ ] **Integration Test**: Enhance `scripts/test_server_pro.sh` to overload the server deliberately (e.g., using fewer workers than required) and assert that:
    - Egress forwarding latency and bitrates remain perfectly stable (No drops).
    - `internal_drop` increases as expected.
    - TR 101 290 CC errors do *not* false-trigger.
