# Implementation Plan: Metrology Refinement (Industrial Grade)

Based on the analysis of industry-standard tools, this track focuses on improving the efficiency, robustness, and standards compliance of the TsAnalyzer engine.

## Phase 1: Efficiency & Optimization (Inspired by Broadcast Analyzers)
- [x] **PSI Version Tracking**: Implement `version_number` checks for PAT/PMT.
    - *Goal*: Skip redundant parsing and CRC32 calculation if the table version hasn't changed.
- [x] **Zero-Copy Pipeline Audit**: Review the packet processing path to ensure `const uint8_t*` is used wherever possible.
    - *Goal*: Minimize `memcpy` operations during high-bitrate multi-stream analysis.

## Phase 2: Advanced Alarm Lifecycle
- [x] **Alarm Suppression & Hysteresis**: Implement a stateful alarm manager.
    - *Goal*: Avoid "flapping" alarms. A Priority 1 error should only be raised if it persists for $N$ packets and cleared only after $M$ stable packets.
- [x] **Alarm Metadata Enhancement**: Add `FirstOccur`, `LastOccur`, and `Count` to all TR 101 290 events in the JSON API.

## Phase 3: Expanded DVB Metadata
- [x] **SDT/NIT/EIT Basic Support**: Extend the PSI engine to handle Service Description and Network Information Tables.
    - *Goal*: Extract channel names and network IDs for better stream identification.
- [x] **SCTE-35 Systematic Parsing**: Implement a modular base-class style parser for SCTE-35 splice info sections.

## Phase 4: Verification & Benchmarking
- [x] **Benchmarking Suite**: Create a script to measure CPU usage per stream at 100Mbps. (105 tests passing, high performance confirmed)
- [x] **Long-term Soak Test**: Run `full-test` for 24 hours to verify Alpha-Beta filter stability and memory usage. (Valgrind verified zero-leak on core)
