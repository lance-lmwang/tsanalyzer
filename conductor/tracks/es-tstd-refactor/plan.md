# ES & T-STD Model Refactor - Implementation Plan (Ultimate Edition)

## Phase 1: Data Structs & Zero-Copy Accumulation
**Goal**: Encapsulate ES state and implement robust PES packet reassembly.

### Task 1.1: Define Consolidated `tsa_es_track_t`
- **Files**: `include/tsa_stream_model.h`, `include/tsa_internal.h`
- **Action**: Migrate all PID-indexed arrays (GOP, frame counts, codec types) into this struct. Add `tsa_pes_state_t` for reassembly tracking.
- **Validation**: Compilation and basic stream info display in `tsa_cli`.

### Task 1.2: Implement PES Payload Accumulator
- **Files**: `src/tsa_es.c`
- **Action**: Implement a state machine to handle cross-packet PES headers. Ensure zero-copy by storing pointers to the `tsa_packet_pool`.
- **Validation**: Verify large PES packet (4K/HEVC) reassembly using a test PCAP.

---

## Phase 2: Frame (AU) Boundary & GOP Analysis
**Goal**: Identify frame types and GOP structures without full decoding.

### Task 2.1: Implement Zero-Copy NALU Sniffer
- **Files**: `src/tsa_es.c`
- **Action**: Implement `tsa_au_sniff()` to detect Access Unit Boundaries (AUB) and Slice Types (I/P/B) for H.264/H.265.
- **Validation**: Use `verify_es_accuracy.sh` and check GOP length reporting accuracy.

### Task 2.2: GOP Stability Metrics
- **Files**: `src/tsa_es.c`
- **Action**: Calculate $N$ (Length), $M$ (Structure), and IDR-to-IDR Interval. Implement variance calculation for GOP stability.
- **Validation**: Verify GOP metrics against a known golden stream.

---

## Phase 3: T-STD Buffer Simulation Engine
**Goal**: Industrial-grade leaky bucket simulation according to Annex D.

### Task 3.1: TB/MB/EB Leaky Bucket Operators
- **Files**: `src/tsa_es.c`
- **Action**: Implement `tsa_tstd_fill()` (ingress) and `tsa_tstd_drain()` (at $t=DTS$). Use fixed-point Q64 math.
- **Validation**: Check `pid_tb_fill` and `pid_eb_fill` levels. Waveform should be a stable sawtooth for CBR.

### Task 3.2: Discontinuity & Sync Recovery
- **Files**: `src/tsa_es.c`
- **Action**: Implement model reset on CC errors or Discontinuity markers. Tie $R_{rx}$ (TB Leak Rate) to the **Phase 1 Physical Bitrate**.
- **Validation**: Inject CC errors using `tsa_chaos_injector.sh` and verify the T-STD model recovers at the next PUSI.

---

## Phase 4: A/V Sync & Temporal Metrology
**Goal**: Measure synchronization and temporal quality.

### Task 4.1: A/V Skew & PTS/DTS Jitter
- **Files**: `src/tsa_es.c`, `src/tsa_engine_pcr.c`
- **Action**: Calculate skew between Video and Audio PTS. Measure PTS/DTS jitter relative to the **Reconstructed STC**.
- **Validation**: Verify that A/V skew matches professional analyzer results.

---

## Phase 5: UI, Metadata & Integration
**Goal**: Export metrics and ensure performance.

### Task 5.1: JSON Snapshot & CLI Integration
- **Files**: `src/tsa_snapshot.c`, `src/tsa_json.c`
- **Action**: Export GOP stability, T-STD fill levels, and A/V skew to the JSON snapshot. Update CLI monitor.
- **Validation**: Run `verify_30s_smoke.sh` and `make test`.
