# Implementation Plan: Advanced Essence Metrology (Loudness & Metadata)

## Status
- [x] **Phase 1: Audio Loudness Engine (Momentary)**
  - [x] Research: Select/Integrate `libebur128` for LUFS calculation.
  - [x] Implement: `src/tsa_audio_audit.c` to extract PCM from AAC/AC3 and push to loudness engine.
  - [x] Implement: Momentary LUFS (400ms) calculation and export to Prometheus.

- [x] **Phase 2: Closed Caption (CC) Presence Monitoring**
  - [x] Implement: Extract EIA-608/708 data from H.264/H.265 SEI NALUs in `src/tsa_es.c`.
  - [x] Implement: Track CC presence per stream and fire `TSA_EVENT_CC_MISSING` if no captions seen for 10s.

- [x] **Phase 3: Deep SCTE-35 Logic Audit**
  - [x] Implement: Cross-reference `splice_insert` with Video GOP structure to verify I-frame alignment.
  - [x] Implement: Event-based logging of full SCTE-35 JSON payloads (Implemented in Lua plugin).

- [x] **Phase 4: Side-car Worker Pool**
  - [x] Refactor: Move loudness and CC parsing to background threads using the `tsa_lua` bridge (Lua scripts run in parallel to data plane).
  - [x] Optimization: Ensure "Zero-Allocation" mandate is maintained in the data plane handoff.

- [x] **Phase 5: Validation & E2E**
  - [x] Test: Use a sample TS with valid CC and Loudness variations (Verified via `tests/test_essence_metrology.c`).
  - [x] Assert: Prometheus metrics reflect the audio level changes and CC status.

## Completion Criteria
1.  **Loudness Accuracy**: Within +/- 0.1 LUFS.
2.  **Responsiveness**: Alarms fire within 1s of a loudness/CC incident.
3.  **Performance**: Data plane throughput drop < 5% with full essence auditing enabled.
