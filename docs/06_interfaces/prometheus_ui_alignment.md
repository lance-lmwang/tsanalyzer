# Prometheus Metrics & UI Alignment Reference

This document provides the definitive mapping between the labels displayed in the **TsAnalyzer Pro NOC Dashboard** (and `tsa_top`) and the corresponding **Prometheus Metrics** exported by the server.

## 1. Primary Status (NOC Header / Fleet Wall)

| UI Label / Logic | Prometheus Metric Name | Description |
| :--- | :--- | :--- |
| **LOCKED / LOST** | `tsa_system_signal_locked` | 1 = Signal Locked, 0 = Lost |
| **HEALTH (Score)** | `tsa_system_health_score` | 0-100 composite health score |
| **PHYS BR** | `tsa_metrology_physical_bitrate_bps`| Physical layer bitrate (bps) |
| **RTT (MS)** | `tsa_transport_srt_rtt_ms` | SRT Round Trip Time |
| **Global Incident**| `tsa_system_global_incident` | 1 if >50% streams are failing |

## 2. TR 101 290 Compliance Matrix (9-Grid)

| UI Grid Label | Prometheus Metric Name | Label / Sub-metric |
| :--- | :--- | :--- |
| **P1.1 SYNC** | `tsa_compliance_tr101290_errors` | `error_type="sync_loss"` |
| **P1.2 PAT** | `tsa_compliance_tr101290_errors` | `error_type="pat_error"` |
| **P1.3 PMT** | `tsa_compliance_tr101290_errors` | `error_type="pmt_error"` |
| **P1.4 CC** | `tsa_compliance_tr101290_errors` | `error_type="cc_error"` |
| **P2.1 TEI** | `tsa_compliance_tr101290_errors` | `error_type="transport_error"` |
| **P2.2 CRC** | `tsa_compliance_tr101290_errors` | `error_type="crc_error"` |
| **P2.3 PCR JIT** | `tsa_metrology_pcr_jitter_ms` | Value > 50ms = Alarm |
| **P2.4 PCR REP** | `tsa_compliance_pcr_repetition_errors` | Continuous counter |

## 3. Essence Vitals (Charts & Timelines)

| UI Label | Prometheus Metric Name | Description |
| :--- | :--- | :--- |
| **FPS** | `tsa_essence_video_fps` | Video frames per second |
| **GOP** | `tsa_essence_gop_ms` | GOP duration in ms |
| **AV SYNC** | `tsa_essence_av_sync_ms` | A/V sync offset in ms |
| **BITRATE** | `tsa_metrology_physical_bitrate_bps`| Timeline bitrate (bps) |

## 4. Deep Diagnostics & Prediction

| UI Label | Prometheus Metric Name | Description |
| :--- | :--- | :--- |
| **RST NET** | `tsa_predictive_rst_network_seconds` | Remaining Safe Time (Network) |
| **RST ENC** | `tsa_predictive_rst_encoder_s` (API) | Remaining Safe Time (Encoder) |
| **MDI-DF** | `tsa_transport_mdi_df_ms` | Delay Factor (Jitter) |
| **DRIFT** | `tsa_metrology_stc_wall_drift_ppm` | Clock drift (PPM) |

## 5. PID Inventory

| UI Column | Prometheus Metric Name | Description |
| :--- | :--- | :--- |
| **PID** | `tsa_metrology_pid_bitrate_bps` | Labeled with `{pid="0x..."}` |
| **TYPE/CODEC**| `tsa_metrology_pid_bitrate_bps` | Labeled with `{type="..."}` |
| **BITRATE** | `tsa_metrology_pid_bitrate_bps` | Per-PID bandwidth |

---
*Note: The NOC Dashboard fetches core metrics from `/metrics/core` and detailed focus data from `/api/v1/snapshot`. Both are derived from the same deterministic metrology engine.*
