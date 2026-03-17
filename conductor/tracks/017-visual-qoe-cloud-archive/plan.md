# Implementation Plan: Visual Quality & Cloud Archive (QoE & S3)

## Status
- [x] **Phase 1: Entropy Analysis for Freeze Detection**
  - [x] Implement: `calculate_shannon_entropy` in `tsa_utils.c`.
  - [x] Implement: Per-PID entropy tracking in `src/tsa_es.c`.
  - [x] Implement: Threshold-based `TSA_EVENT_ENTROPY_FREEZE` trigger.
  - [x] Validate: Verified via `tests/test_essence_metrology.c`.

- [ ] **Phase 2: Sidecar IDR Thumbnailer**
  - [x] Research: Integrate lightweight IDR extraction (using `FFmpeg` or direct NALU dumping).
  - [ ] Implement: Background worker to save JPEG/Base64 thumbnails.

- [ ] **Phase 3: Event-Triggered S3 Archive**
  - [ ] Implement: Link `tsa_forensic_trigger` to automated S3 upload script.
  - [ ] Implement: Rolling 2-minute "Time Machine" in memory.

## Completion Criteria
1.  **Freeze Detection**: 100% detection rate for black/static screens within the specified window.
2.  **Visual Proof**: Thumbnails correctly reflect stream content.
3.  **Archival**: Cloud upload success for 100% of triggered critical events.
