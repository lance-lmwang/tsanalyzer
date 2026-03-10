# Implementation Plan: Industrial Alignment (libeasyice Parity)

**Goal**: Align `tsanalyzer` with industrial standard practices from `libeasyice` to improve forensic accuracy, HLS robustness, and protocol depth.

## Phase 1: Precision Forensics (Byte-Offset Tracking)
**Goal**: Match `libeasyice`'s ability to locate errors by exact file byte offset.
- [x] **Task 1.1: Extend Event Metadata**
    - **Action**: Add `uint64_t absolute_byte_offset` to `tsa_event_t` in `include/tsa_event.h`.
- [x] **Task 1.2: Global Byte Tracking**
    - **Action**: Update `tsa_handle_t` to maintain a monotonic `processed_bytes` counter. Increment in `tsa_feed_data`.
- [x] **Task 1.3: Forensic Exporter Update**
    - **Action**: Update `src/tsa_json.c` and Prometheus metrics to export the byte offset for every TR 101 290 and T-STD violation.
- **Validation**: Replay a TS file with a known error at offset X and verify the JSON report shows offset X.

## Phase 2: HLS Ingest & Buffer Robustness
**Goal**: Implement `libeasyice` style segment download state machine and stability metrics.
- [x] **Task 2.1: Segment Download Queue & Retry**
    - **Action**: Refactor `src/tsa_hls_parser.c` to include a segment queue with retry logic (3 retries, exponential backoff) similar to `cdownloader.cpp`.
- [x] **Task 2.2: Buffer Stability Analysis (RST)**
    - **Action**: Implement **RST (Remaining Safe Time)** calculation.
    - **Formula**: $RST = \sum(Segment\_Durations) - (Wall\_Clock - Sync\_Time)$.
- [x] **Task 2.3: HLS Metadata Export**
    - **Action**: Add `hls_buffer_ms` and `hls_download_errors` to the metrics engine.
- **Validation**: Simulate a 500ms network dropout during HLS ingest and verify the engine doesn't crash and correctly reports RST drop.

## Phase 3: PSI/SI Hardening & Section Edge Cases
**Goal**: Align with `PsiCheck.cpp` for robust handling of fragmented sections.
- [x] **Task 3.1: Section Fragment Audit**
    - **Action**: Audit `src/tsa_psi.c` to ensure `pointer_field` logic handles cases where a new section starts in the middle of a TS packet already containing an old section's tail.
- [x] **Task 3.2: DVB Descriptor Expansion**
    - **Action**: Port descriptors for **Service Name**, **Provider Name**, and **LCN (Logical Channel Number)** from `libeasyice` tables definitions.
- **Validation**: Use `test_fragmented_psi.c` to verify 100% CRC pass rate on complex MPTS streams.

## Phase 4: Content Layer Enrichment (GOP & Metadata)
**Goal**: Improve GOP visualization and frame distribution statistics.
- [x] **Task 4.1: Frame Distribution Histogram**
    - **Action**: Maintain a sliding window histogram of I/P/B frame sizes per PID.
- [x] **Task 4.2: GOP Structure String**
    - **Action**: Export the GOP pattern (e.g., `IBBPBBP...`) as a metadata string in the JSON API.
- **Validation**: Compare GOP string output against `libeasyice`'s GOP list for the same sample file.
