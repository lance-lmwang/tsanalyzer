# ES & T-STD Model Refactor - Implementation Plan (Ultimate Director's Edition)

## Phase 0: Metrology & STC Refinement (Hotfix Task) [x]
**Status**: COMPLETED (Verified in Commit 7e98881)
- [x] LRM Numerical Stability (Baseline Offset)
- [x] Predictive Loop Suppression (Validity Checks)

---

## Phase 1: Data Structs & Zero-Copy Accumulation
**Goal**: Encapsulate ES state and implement robust PES packet reassembly.

### Task 1.1: Define Consolidated `tsa_es_track_t` [x]
- **Files**: `include/tsa_stream_model.h`, `include/tsa_internal.h`, `include/tsa_es_track.h`
- **Action**: Migrate all PID-indexed arrays (GOP, frame counts, codec types) into this consolidated struct. Add the `accumulator` struct for state tracking.
- **Validation**: COMPLETED. Struct defined in `include/tsa_es_track.h`.

### Task 1.2: Implement PES Payload Accumulator [/]
- **Files**: `src/tsa_es.c`
- **Status**: IN PROGRESS. Initial zero-copy reference logic implemented in `src/tsa_es.c`.

---

## Phase 2: Frame (AU) Boundary & GOP Analysis
**Goal**: Identify frame types and GOP structures without full decoding.

### Task 2.1: Implement Zero-Copy NALU Sniffer [x]
- **Files**: `src/tsa_es.c`
- **Action**: Implement `tsa_au_sniff()` (via `tsa_nalu_sniff`) to detect Access Unit Boundaries (AUB) and Slice Types (I, P, B, IDR) for H.264/H.265 directly from the packet pool.
- **Validation**: COMPLETED. Integrated into `tsa_handle_es_payload`.

### Task 2.2: GOP Stability Metrics [x]
- **Files**: `src/tsa_es.c`
- **Action**: Calculate $N$ (Length), $M$ (Structure), and IDR-to-IDR Interval. Implement an Exponential Moving Average (EMA) for GOP length to measure stability.
- **Validation**: COMPLETED. EMA and min/max tracking implemented.

---

## Phase 3: T-STD Buffer Simulation Engine
**Goal**: Industrial-grade leaky bucket simulation according to ISO/IEC 13818-1 Annex D.

### Task 3.1: TB/MB/EB Leaky Bucket Operators
- **Files**: `src/tsa_es.c`
- **Action**: Implement `tsa_tstd_fill()` (ingress) and `tsa_tstd_drain()` (at $t=DTS$) using fixed-point Q64 math.
- **Validation**: Check `pid_tb_fill` and `pid_eb_fill` levels. The waveform must be a stable sawtooth for CBR streams.

### Task 3.2: DTS Extrapolation & Sync Recovery
- **Files**: `src/tsa_es.c`
- **Action**: Implement DTS extrapolation for PES headers missing DTS. Implement model reset on CC errors/Discontinuity markers.
- **Validation**: Inject CC errors using `tsa_chaos_injector.sh` and verify the T-STD model recovers gracefully at the next PUSI.

### Task 3.3: Dynamic Leak Rate Sync
- **Files**: `src/tsa_es.c`, `src/tsa_engine_pcr.c`
- **Action**: Dynamically tie $R_{rx}$ (TB Leak Rate) to the real-time Physical Bitrate calculated in Phase 1.
- **Validation**: Run `verify_realtime_metrology.sh` to ensure buffer simulation does not drift over long durations.

---

## Phase 4: A/V Sync & Violation Monitoring
**Goal**: Measure synchronization and trigger alerts for buffer violations.

### Task 4.1: A/V Skew & PTS/DTS Jitter
- **Files**: `src/tsa_es.c`, `src/tsa_engine_pcr.c`
- **Action**: Calculate skew between Video and Audio PTS. Measure PTS/DTS jitter relative to the Reconstructed STC.
- **Validation**: Verify that A/V skew matches professional lab-grade analyzer results.

### Task 4.2: T-STD Violation Detection
- **Files**: `src/tsa_es.c`, `src/tsa_alert.c`
- **Action**: Implement Overflow (fill > max) and Underflow (DTS < STC) detection logic. Trigger appropriate alerts.
- **Validation**: Feed a known-broken TS stream and verify that underflow alerts are correctly generated.

---

## Phase 5: UI, Metadata & Integration
**Goal**: Export metrics and ensure performance.

### Task 5.1: JSON Snapshot & CLI Integration
- **Files**: `src/tsa_snapshot.c`, `src/tsa_json.c`, `src/tsa_cli.c`
- **Action**: Export GOP stability, T-STD fill levels, and A/V skew to the JSON snapshot. Update the CLI monitor UI to display these metrics.
- **Validation**: Run `verify_30s_smoke.sh` and execute `make test` for memory leak (valgrind) checks.
