# ES & T-STD Model Refactor - Implementation Plan (Enhanced)

## Phase 1: Data Structs & Atomic Tracking
- **Task 1.1**: Define `tsa_es_track_t` (Consolidated).
- **Task 1.2**: Implement **PES Payload Accumulator**. Handle cross-packet headers. Ensure Zero-Copy using `tsa_packet_pool`.

## Phase 2: Frame (AU) Analysis
- **Task 2.1**: Implement **NALU Sniffer**. Extract Slice Type (I/P/B) and AUB for H.264/H.265.
- **Task 2.2**: Calculate GOP metrics: $N$ (Length), $M$ (Structure), and IDR-to-IDR Interval.

## Phase 3: Leaky Bucket & Metrology Loop
- **Task 3.1**: TB/MB/EB Simulation using fixed-point math.
- **Task 3.2**: **Discontinuity & Sync Recovery**. Implement reset logic on CC errors/Discontinuity markers to prevent model drift.
- **Task 3.3**: Tie $R_{rx}$ (TB Leak Rate) to the **Phase 1 Physical Bitrate**.

## Phase 4: A/V Sync & Temporal Metrology
- **Task 4.1**: Link Video PTS and Audio PTS using the same Program reference.
- **Task 4.2**: Calculate temporal skew and PTS/DTS jitter relative to the **Reconstructed STC**.

## Phase 5: Verification & UI
- **Verification**: Use `verify_es_accuracy.sh`. Validate buffer stability on 10M CBR streams.
- **Integration**: Push GOP and T-STD metrics to the JSON snapshot for Grafana.
