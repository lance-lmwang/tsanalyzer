# ES & T-STD Model Refactor - Implementation Plan (Ultimate Director's Edition)

## Phase 0: Metrology & STC Refinement (Hotfix Task) [x]
**Status**: COMPLETED (Verified in Commit 7e98881)
- [x] LRM Numerical Stability (Baseline Offset)
- [x] Predictive Loop Suppression (Validity Checks)

---

## Phase 1: Data Structs & Zero-Copy Accumulation [x]
**Goal**: Encapsulate ES state and implement robust PES packet reassembly.

### Task 1.1: Define Consolidated `tsa_es_track_t` [x]
- **Files**: `include/tsa_stream_model.h`, `include/tsa_internal.h`, `include/tsa_es_track.h`
- **Action**: Migrate all PID-indexed arrays (GOP, frame counts, codec types) into this consolidated struct. Add the `accumulator` struct for state tracking.
- **Validation**: COMPLETED. Struct defined in `include/tsa_es_track.h`.

### Task 1.2: Implement PES Payload Accumulator [x]
- **Files**: `src/tsa_es.c`, `src/tsa_engine_essence.c`
- **Status**: COMPLETED. Robust zero-copy PES accumulator implemented via `tsa_es_track_push_packet`. Improved PES header parsing and centralized state management. Verified via `test_qoe_pro` and `test_tstd_model`.

---

## Phase 2: Frame (AU) Boundary & GOP Analysis [x]
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

## Phase 3: T-STD Buffer Simulation Engine [x]
**Goal**: Industrial-grade leaky bucket simulation according to ISO/IEC 13818-1 Annex D.

### Task 3.1: TB/MB/EB Leaky Bucket Operators [x]
- **Files**: `src/tsa_es.c`
- **Action**: Implement `tsa_tstd_fill()` (ingress) and `tsa_tstd_drain()` (at $t=DTS$) using fixed-point Q64 math.
- **Validation**: Check `pid_tb_fill` and `pid_eb_fill` levels. The waveform must be a stable sawtooth for CBR streams.

### Task 3.2: DTS Extrapolation & Sync Recovery [x]
- **Files**: `src/tsa_es.c`
- **Action**: Implement DTS extrapolation for PES headers missing DTS. Implement model reset on CC errors/Discontinuity markers.
- **Validation**: COMPLETED. DTS extrapolation implemented in `tsa_es_track_push_packet`. Verified via `test_tstd_expert`.

### Task 3.3: Dynamic Leak Rate Sync [x]
- **Files**: `src/tsa_es.c`, `src/tsa_engine_essence.c`, `include/tsa_internal.h`
- **Action**: Implement `tsa_tstd_sync_params()` to calculate ISO/IEC 13818-1 leak rates ($R_{rx}$ and $R_{bx}$) based on detected H.264/H.265 profile and level. Update `tsa_tstd_update_leak` to use these dynamic rates.
- **Validation**: COMPLETED. Rates are now sync'd upon SPS detection. Fallback logic handles streams with missing metadata. Verified via compilation and unit tests.
