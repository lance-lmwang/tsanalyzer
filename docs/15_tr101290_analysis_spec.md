# TsAnalyzer 2.0: Professional Metrology & Analysis Specification

This document defines the implementation standards for Broadcast-Grade TS analysis in TsAnalyzer, incorporating advanced algorithms for PCR accuracy, buffer modeling, and network correlation.

## 1. Core Metrology Engine (TR 101 290)

### 1.1 Priority 1: Service Fatal Errors (Availability)
**Focus**: Is the stream decodable?

| Metric | TR 101 290 Ref | Threshold | Implementation Logic |
| :--- | :--- | :--- | :--- |
| **TS_sync_loss** | P 1.1 | 5 bad / 2 good | Loses lock after 5 consecutive sync byte errors. Recovers after 2 consecutive correct sync bytes (0x47). |
| **Sync_byte_error** | P 1.2 | Immediate | Triggered for every TS packet where Byte 0 != 0x47. |
| **PAT_error** | P 1.3 | 500ms | Triggered if PAT (PID 0) is not seen for > 500ms or if Table_ID is not 0x00. |
| **Continuity_error** | P 1.4 | Immediate | Triggered if the Continuity Counter (CC) sequence is broken for a PID (excluding duplicates and discontinuity flags). |
| **PMT_error** | P 1.5 | 500ms | Triggered if a PMT (defined in PAT) is not seen for > 500ms on its designated PID. |
| **PID_error** | P 1.6 | 5.0s | Triggered if a referenced PID (Video/Audio/PCR) from the PMT is not seen for > 5.0s. |

### 1.2 Priority 2: Transport & Timing (Quality of Experience)
**Focus**: Is the stream stable for long-term playout?

| Metric | TR 101 290 Ref | Threshold | Implementation Logic |
| :--- | :--- | :--- | :--- |
| **Transport_error** | P 2.1 | Immediate | Triggered if the `transport_error_indicator` bit in the TS header is set to 1. |
| **CRC_error** | P 2.2 | Immediate | Triggered if the CRC32 check for PAT, PMT, NIT, or SDT sections fails. |
| **PCR_repetition** | P 2.3 | 40ms | Triggered if the interval between two PCRs on the same PID exceeds 40ms. |
| **PCR_accuracy** | P 2.4 | ± 500ns | Triggered if the PCR overall jitter (accuracy) exceeds ± 500ns relative to the local system clock (using `CLOCK_MONOTONIC_RAW`). |

---

## 2. Advanced Diagnostic Layer (Expert Mode)

### 2.1 SI/PSI Tree Decomposition (StreamXpert Style)
TsAnalyzer recursively parses the sub-structures to ensure service-level integrity:
- **Descriptor Parsing**: Full support for `Service_descriptor`, `Component_descriptor`, and `Extended_event_descriptor`.
- **Version Tracking**: Real-time monitoring of `version_number` changes in PAT/PMT to trigger fast-reparsing and state refresh.

### 2.2 PCR Jitter & Drift Analysis (MTS4000 Style)
- **PCR Overall Jitter (OJ)**: The combination of network packet delay variation and encoder inaccuracy.
- **PCR Frequency Offset**: Tracking the 27 MHz clock deviation in **ppm** (parts per million) via long-term linear regression.

---

## 3. Network-to-Media Correlation (The SRT/IP Edge)
TsAnalyzer implements a unique **Correlation Matrix** to distinguish between Source (Encoder) and Transport (Network) issues:

| Observation | Primary Cause | Action |
| :--- | :--- | :--- |
| MDI-MLR > 0 + CC Errors | **Network Loss** | Trigger SRT ARQ / Buffer Increase |
| MDI-MLR = 0 + CC Errors | **Encoder Issue** | Signal Source Fault |
| Stable MDI + PCR Jitter | **Encoder Clock Drift** | Monitor Frequency Offset (ppm) |

---

## 4. Forensic Integration

### 4.1 Rolling Buffer & Trigger
- **Circular Buffer**: Maintains a 30-second RAM buffer of raw TS packets.
- **Automatic Freeze**: Upon any P1 error or PCR Drift > 1000ns, the buffer is dumped to a `.ts` file with a `.json` metadata report for offline analysis.

### 4.2 API Integration
- **JSON Snapshot**: `GET /api/v1/metrology/full`
- **Prometheus/Grafana**: Custom exporter for long-term stability monitoring.

---

## 5. Implementation Notes for Developers

- **Precision Timing**: On Linux, use `clock_gettime(CLOCK_MONOTONIC_RAW)` to obtain nanosecond timestamps. This avoids NTP slewing and provides the most stable baseline for PCR Accuracy measurements.
- **UI UX Strategy**: Implement a "Traffic Light" (Red/Green/Yellow) status grid benchmarked against **DekTec StreamXpert**. Use the `tsa_alarm_t` structure to populate the "Error Message" column in the NOC dashboard.
