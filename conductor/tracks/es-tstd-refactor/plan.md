# ES & T-STD Model Refactor - Implementation Plan

## Phase 1: State Consolidation & Tracking
**Goal**: Encapsulate ES state to remove array sprawl in `tsa_handle`.

### Task 1.1: Define `tsa_es_track_t`
- **Files**: `include/tsa_stream_model.h`, `include/tsa_internal.h`
- **Action**: Migrate PID-indexed arrays (GOP, frame counts, codec types) into this consolidated struct.
- **Validation**: Compilation and basic stream info display.

---

## Phase 2: Frame Boundary (AUB) Identification
**Goal**: Standardize frame tracking for H.264/H.265.

### Task 2.1: Implement `tsa_au_sniff()`
- **Files**: `src/tsa_es.c`
- **Action**: Implement zero-copy NALU header sniffing to detect AU boundaries and frame types.
- **Validation**: Use `verify_es_accuracy.sh` and check GOP length reporting.

---

## Phase 3: T-STD Buffer Simulation Engine
**Goal**: Professional-grade leaky bucket simulation.

### Task 3.1: TB/MB/EB Simulation Operators
- **Files**: `src/tsa_es.c`
- **Action**: 
    - Implement `tsa_tstd_fill()` for each TS packet ingress.
    - Implement `tsa_tstd_drain()` for each AU (Access Unit) DTS removal.
- **Validation**: Check `pid_tb_fill` and `pid_eb_fill` outputs against a golden CBR stream.

---

## Phase 4: A/V Skew & Drift Analysis
**Goal**: Measure synchronization quality.

### Task 4.1: PTS vs STC Calculation
- **Files**: `src/tsa_es.c`, `src/tsa_engine_pcr.c`
- **Action**: Calculate the difference between the reconstructed STC (from Metrology Refactor) and the PES PTS.
- **Validation**: Verify that A/V skew matches the delta seen in lab-grade analyzers.

---

## Phase 5: UI & Metadata Integration
- **Task**: Update JSON snapshot to export the new T-STD metrics.
- **Task**: Implement GOP stability (variance) metric.
- **Task**: Run memory leak and performance stress tests.
- **Validation**: `make test` and `verify_30s_smoke.sh`.
