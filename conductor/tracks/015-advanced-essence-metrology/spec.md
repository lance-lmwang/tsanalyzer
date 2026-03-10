# Specification: Advanced Essence Metrology (Loudness & Metadata)

## 1. Objective
Deepen the L4 (Essence) observation tier by adding professional audio loudness measurement, full-stack SCTE-35 splicing audit, and subtitle/Closed Caption presence verification.

## 2. Requirements
- **Audio Loudness (LUFS)**: Implement EBU R128 / ITU-R BS.1770-4 standard calculation for 2.0 and 5.1 audio.
- **SCTE-35 Logic Audit**: Beyond presence detection, verify the consistency of `splice_insert` commands against physical signals (SCTE-104) and playout schedules.
- **Subtitle/CC Presence**: Monitor DVB Subtitles and EIA-608/708 Closed Captions for presence and basic structure (parity check).
- **Metadata Telemetry**: Export `loudness_momentary_lufs`, `loudness_integrated_lufs`, and `scte35_event_id` to Prometheus.

## 3. Architecture: Side-car Metadata Workers
- **Isolation**: Heavy metadata parsing (e.g., extracting CC from SEI NALUs) will be offloaded to a non-blocking `metadata_worker` pool to avoid data plane jitter.
- **Loudness Engine**: Use a high-efficiency floating-point library (e.g., `libebur128`) for the 400ms momentary window.
- **Triggered SCTE-35 Snapshot**: Automatically log the full base64 SCTE-35 XML/JSON for every splice event for post-event forensic audit.

## 4. Performance Targets
- **Scalability**: Support loudness auditing for up to 500 concurrent audio tracks per appliance core.
- **Accuracy**: Within +/- 0.1 LUFS of the industry-standard hardware meters.
