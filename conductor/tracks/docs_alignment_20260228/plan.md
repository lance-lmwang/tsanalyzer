# Implementation Plan: Docs Alignment (First 10 Documents)

## Phase 1: Core Architecture & Determinism (Docs 01, 05)
- [x] **Task 1.1: Eliminate Floating-Point Math**
    - [x] **Action**: Replace `double` with `int128_t` (Q64.64 fixed-point) in `ts_pcr_window_regress` and `calculate_pcr_jitter`.
    - [x] **Action**: Update `tsa_internal.h` and `src/tsa.c` to use `TO_Q64_64` for all VSTC and jitter calculations.
    - [x] **Action**: Ensure `tsa_config_t.pcr_ema_alpha` is converted to fixed-point logic.
- [x] **Task 1.2: Thread Topology & CPU Affinity**
    - [x] **Action**: Refactor `src/tsa_main.c` to spawn 4 distinct threads: Capture, Decode, Metrology, Output.
    - [x] **Action**: Implement `pthread_setaffinity_np` for each thread (e.g., locking to Core 0, 1, 2, 3).
- [x] **Task 1.3: Lock-Free SPSC Queue & Memory Wall**
    - [x] **Action**: Implement a lock-free SPSC ring buffer in a new file `src/spsc_queue.c` and `include/spsc_queue.h`.
    - [x] **Action**: Integrate SPSC between Capture -> Decode -> Metrology -> Output.
    - [x] **Action**: Use `alignas(64)` for queue descriptors to prevent false sharing.
- [x] **Task 1.4: Hardware Timestamping Integration**
    - [x] **Action**: Modify ingest to extract `SO_TIMESTAMPING` hardware timestamps where supported.
    - [x] **Action**: Fallback strictly to `CLOCK_MONOTONIC_RAW` if hardware timestamping is unavailable.
- [x] **Task: Conductor - User Manual Verification 'Phase 1: Core Architecture & Determinism' (Protocol in workflow.md)**

## Phase 2: Timing, Buffer Physics & Replay (Docs 02, 03, 08)
- [x] **Task 2.1: VSTC Reconstruction**
    - [x] **Action**: Implement interpolation slope using Q64.64 math in `tsa_process_packet`.
    - [x] **Action**: Assign a unique immutable VSTC to each processed packet and Access Unit (AU).
- [x] **Task 2.2: T-STD Buffer Model & RST**
    - [x] **Action**: Refactor `pid_eb_fill_bytes` to use Q64.64 representation.
    - [x] **Action**: Implement Leaky Bucket math: `Buffer = Buffer - DrainRate * ΔVSTC + AU_Size` in `tsa.c`.
    - [x] **Action**: Calculate RST (Remaining Safe Time) = `Buffer / DrainRate` and add to JSON export.
- [x] **Task 2.3: Deterministic Replay & Validation Suite**
    - [x] **Action**: Create `scripts/verify_determinism.sh` that runs the analyzer twice on a sample TS and compares the MD5 of the JSON output.
    - [x] **Action**: Add CI test to verify bit-identical results.
- [x] **Task: Conductor - User Manual Verification 'Phase 2: Timing, Buffer Physics & Replay' (Protocol in workflow.md)**

## Phase 3: Error Model & Operational Modes (Docs 07, 09)
- [x] **Task 3.1: Strict Error Propagation**
    - [x] **Action**: Implement state tracking (`VALID`, `DEGRADED`, `INVALID`) per PID in `tsa_internal.h`.
    - [x] **Action**: Update CC error handler to immediately set PID state to `DEGRADED`.
    - [x] **Action**: Add `absolute_byte_offset` and `triggering_vstc` to `tsa_alarm_t` struct.
- [x] **Task 3.2: Operational Mode Guard**
    - [x] **Action**: Add mode selection CLI flag (`--mode=live|replay|forensic|certification`).
    - [x] **Action**: Enforce `isolcpus` check and `SO_TIMESTAMPING` check when in `certification` mode.
    - [x] **Action**: Include the operational mode and version hash in the `/api/v1/metrology/full` JSON output.
- [x] **Task: Conductor - User Manual Verification 'Phase 3: Error Model & Operational Modes' (Protocol in workflow.md)**
