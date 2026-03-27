# Metrology Refactor - Implementation Plan

## Phase 1: Infrastructure & Units Standardization
**Goal**: Normalize PCR extraction and time conversion across the entire codebase.

### Task 1.1: Standardize PCR Extraction
- **Files**: `include/tsa_bitstream.h`, `include/tsp.h`
- **Action**:
    - Implement `static inline uint64_t tsa_pkt_get_pcr(const uint8_t *pkt)` with full 42-bit reconstruction.
    - Implement `static inline uint64_t tsa_pcr_to_ns(uint64_t pcr_ticks)`.
- **Validation**: `make && ./tests/test_bitstream_pcr`

### Task 1.2: Cleanup Redundant Converters
- **Files**: `src/tsa_clock.c`, `src/tsa_engine_pcr.c`
- **Action**: Replace all manual bit-shifts and `* 1000 / 27` with the new standardized macros.
- **Validation**: Ensure `verify_pcr_repetition.sh` still passes with 0 regressions.

---

## Phase 2: Metrology Track Consolidation
**Goal**: Encapsulate per-PID metrology state into a single, cohesive structure.

### Task 2.1: Define `tsa_pcr_track_t`
- **Files**: `include/tsa_clock.h` (or a new `tsa_metrology.h`)
- **Action**: Migrate fields from `tsa_clock_inspector_t` and `tsa_handle` into this new struct.
- **Validation**: Compilation only.

### Task 2.2: Implement Linear Regression Migration
- **Files**: `src/tsa_clock.c`
- **Action**: Move `ts_pcr_window_regress` logic into `tsa_pcr_track_update`. Enable LRM for ALL PCR-carrying PIDs, not just the master.
- **Validation**: Run `verify_realtime_metrology.sh`. Jitter should be more stable under network burst simulations.

---

## Phase 3: Engine Logic Decoupling
**Goal**: Remove business logic from `tsa_engine_pcr.c` and turn it into a thin wrapper.

### Task 3.1: Refactor `pcr_on_ts`
- **Files**: `src/tsa_engine_pcr.c`
- **Action**:
    1. Call `tsa_pkt_get_pcr`.
    2. Feed to `tsa_pcr_track_update`.
    3. Push metrics to `h->live` snapshot directly from the track state.
- **Validation**: `scripts/test_pcr_e2e.sh`

---

## Phase 4: MPTS Isolation & Aggregation
**Goal**: Ensure multi-program streams don't pollute global metrics.

### Task 4.1: Program-Aware Metrics
- **Files**: `include/tsa_internal.h`, `src/tsa_snapshot.c`
- **Action**:
    - Replace `h->live->pcr_jitter_max_ns` with a per-program lookup.
    - Implement a "Master Clock Election" per PMT.
- **Validation**: Load a multi-program PCAP. Verify each program has its own independent jitter/drift stats in the CLI monitor.

---

## Phase 5: Final Cleanup & Performance Audit
- **Task**: Remove `br_calc` and old `pcr_window` fields from `tsa_handle`.
- **Task**: Run `valgrind` to ensure the new dynamic LRM windows (if any) are properly freed.
- **Task**: Execute `make format && make test`.
