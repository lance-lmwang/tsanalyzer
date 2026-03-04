# Specification: libeasyice Parity & Professional Robustness (20260304)

## 1. Background and Motivation
The current `tsanalyzer` has established a robust baseline for synchronized metrology (Sync-Lock, PCR VSTC, Sampling Barriers). However, when benchmarked against industrial-grade tools like `libeasyice`, gaps remain in comprehensive protocol decoding and deep validation. Specifically, the parser currently assumes a simplified SI/PSI layout and lacks a full TR 101 290 state machine. 

To achieve full "Measurement Instrument" status, `tsanalyzer` must adopt `libeasyice`'s architectural rigor in section filtering, table reassembly, and comprehensive error probing.

## 2. Architectural Objectives

### 2.1 Stateful Section Assembly (PSI/SI Filtering)
- **Current State:** The engine naively parses the first packet of a PAT/PMT/SDT assuming it fits within a single 188-byte payload. It does not validate CRC32 or handle multi-packet tables.
- **Target State (libeasyice style):** Implement a Section Filter Engine (similar to `libdvbpsi` integration in `libeasyice`).
  - Reassemble tables split across multiple TS packets.
  - Implement strict CRC32 validation before accepting table data.
  - Track `version_number` to seamlessly handle dynamic channel map updates.

### 2.2 Comprehensive TR 101 290 State Machines
- **Current State:** Basic counters exist for CC errors, Sync loss, and PCR Jitter.
- **Target State:** Implement dedicated State Machines for Priority 1, 2, and 3 parameters (`TrCore` style).
  - **Timeouts:** Track individual PID last-seen timestamps to enforce PAT <= 500ms, PMT <= 500ms, SDT <= 2000ms.
  - **Repetition Rates:** Validate PCR interval <= 40ms constraint properly against the VSTC.
  - **Unreferenced PIDs:** Detect active streams that are not declared in any PMT (Ghost PIDs).

### 2.3 Deep Elementary Stream (ES) Inspection
- **Current State:** Heuristic NALU/ADTS probing exists but stops at identifying the codec.
- **Target State:** Parse into the PES layer to extract presentation timing and frame metadata.
  - Extract exact `PTS` and `DTS` values to calculate AV Sync drift accurately.
  - Detect Video `I/P/B` frame types for true GOP (Group of Pictures) length measurement.
  - Measure true Payload Bitrate (excluding TS/PES headers) vs Transport Bitrate.

## 3. Scope of Work
- **DO NOT** rewrite the existing `Sync-Lock` or `VSTC` metrology engines, as they are functioning perfectly.
- **DO** refactor `tsa_decode_packet` and `process_pat`/`process_pmt` to route through a new `tsa_section_assembler`.
- **DO** expand `tsa_measurement_status_t` to hold discrete TR 101 290 alarm latches.
- **DO** implement automated testing against complex, multi-packet SI tables to ensure robustness.

## 4. Success Criteria
- The engine can successfully parse and validate a PAT/PMT/SDT/EIT that is fragmented across 3+ TS packets.
- The Dashboard accurately reports `TSA_STATUS_DEGRADED` if a table `version_number` changes but the CRC is invalid.
- AV Sync drift and Video FPS are accurately reported from deep PES inspection rather than heuristic guessing.
