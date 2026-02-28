# TsAnalyzer: Industrial-Grade TR 101 290 Metrology Specification

This document defines the rigorous mathematical implementation of the ETSI TR 101 290 standard and associated ISO/IEC 13818-1 models.

---

## 1. Deterministic Timing & Metrology Foundation

All metrology in TsAnalyzer is driven by the **[Timing Model](./02_timing_model.md)** and the **[Metrology Model](./04_metrology_model.md)**.
- **Clock Reconstruction**: The **Virtual System Time Clock (VSTC)** provides the unified 64-bit linear axis for all measurements.
- **Traceability**: Every reported violation is linked to an **Absolute Byte Offset** and a physical causal chain.
- **No Smoothing**: In accordance with the "Software Oscilloscope" philosophy, the engine reports raw physical violations without temporal averaging.

---

## 2. ETSI TR 101 290 Priority 1: Service Availability

| Metric | ID | Strict Threshold | Implementation Logic |
| :--- | :--- | :--- | :--- |
| **TS_sync_loss** | P 1.1 | 5-In / 2-Out | **Strict FSM**: 5 consecutive 0x47 to lock; 2 consecutive errors to loss. |
| **Sync_byte_error** | P 1.2 | Every packet | Triggered if Byte 0 != 0x47. |
| **PAT_error** | P 1.3 | 500ms | PID 0 interval > 13.5M V-STC units or Table_ID != 0x00. |
| **Continuity_error** | P 1.4 | Immediate | CC sequence check per PID, respecting Discontinuity Indicator. |
| **PMT_error** | P 1.5 | 500ms | Referenced PMT interval > 13.5M V-STC units. |
| **PID_error** | P 1.6 | 5.0s | Referenced PID (Video/Audio/PCR) interval > 135M V-STC units. |

---

## 3. ETSI TR 101 290 Priority 2: Quality & Timing

| Metric | ID | Strict Threshold | Implementation Logic |
| :--- | :--- | :--- | :--- |
| **Transport_error** | P 2.1 | Immediate | `transport_error_indicator` bit set to 1. |
| **CRC_error** | P 2.2 | Immediate | CRC32 mismatch for PAT, PMT, CAT, NIT, SDT, EIT. |
| **PCR_repetition** | P 2.3 | 40ms | Interval between PCRs > 1,080,000 V-STC units. |
| **PCR_accuracy** | P 2.4 | ± 500ns | **Decomposed metrology**: PCR_AC (Accuracy), PCR_DR (Drift), PCR_OJ (Jitter). |
| **PTS_error** | P 2.5 | 700ms | PTS interval > 18.9M V-STC units (90kHz). |
| **CAT_error** | P 2.6 | Immediate | Table_ID != 0x01 when scrambling bit is set. |

---

## 4. ISO/IEC 13818-1 Annex D: Buffer Simulation

Buffer health metrology is driven by the **[Buffer Model](./03_buffer_model.md)**.
- **T-STD Simulation**: TsAnalyzer implements a time-locked leaky bucket for every active PID.
- **Determinism**: Simulation is driven by **VSTC** and **Access Units (AU)**, ensuring bit-identical results across replay instances.
- **Predictive Metrics**: The **RST (Remaining Safe Time)** is derived directly from real-time $B_n$ fullness.

---

## 5. Advanced SCTE-35 Ad-Insertion Audit

*   **Splice_Time Alignment**: Compares the `pts_time` in SCTE-35 with the actual PTS of the nearest IDR/I-Frame.
*   **Drift Measurement**: Calculates the **Alignment Offset in 90kHz ticks**.
*   **Pre-roll Compliance**: Automatically verifies if the cue tone arrived at the target lead time (e.g., 4000ms ± 100ms).
*   **Sync Audit**: Ensures SCTE-35 markers are bit-identical and synchronized across all ABR profiles.

---

## 6. Industrial Content & Essence Compliance (Side-car Strategy)

To maintain 1Gbps line-rate, essence audits utilize a sparse sampling side-car decoder.

### 6.1 Audio Loudness (ITU-R BS.1770-4 / EBU R128)
*   **Metering**: Integrated (Long-term), Short-term (3s), and Momentary (400ms) LUFS.
*   **Target**: Alarm on ± 2.0 LU deviation from target -23/-24 LKFS.

### 6.2 Visual Analysis (Partial IDR Decoding)
*   **Black Frame**: Analyzes luma distribution of sampled IDR frames. Alarm if **> 95% pixels < 16** (legal black).
*   **Freeze Detection**: Compares **Pixel Fingerprints** of consecutive IDR frames. Alarm if static for **> 2.0s** while PTS progresses.
*   **AV-Sync (Lip-Sync)**: Real-time calculation of PTS-offset. Alarm on deviation **> ±40ms**.

### 6.3 Closed Caption Audit (CEA-608 / CEA-708)
*   **Integrity**: Monitors Picture User Data or SEI payloads for continuous CC data stream.
*   **Sync**: Verifies CC synchronization with video frame rate.

---

## 7. Forensic Output & Micro-Capture

*   **Forensic JSON**: Every error includes `byte_offset`, `hw_timestamp`, `v_stc`, and `engine_version`.
*   **Micro-Capture**: 200ms (100ms pre/post) raw TS snippet recorded automatically on any P1 trigger.
