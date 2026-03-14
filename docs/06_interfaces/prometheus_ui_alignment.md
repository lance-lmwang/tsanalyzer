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
| **P1.1 SYNC** | `tsa_tr101290_p1_sync_loss_total` | Total sync loss count |
| **P1.2 PAT** | `tsa_tr101290_p1_pat_errors_total` | PAT timeout errors |
| **P1.3 PMT** | `tsa_tr101290_p1_pmt_errors_total` | PMT timeout errors |
| **P1.4 CC** | `tsa_tr101290_p1_cc_errors_total` | Continuity counter errors |
| **P2.1 TEI** | `tsa_tr101290_p2_tei_errors_total` | Transport Error Indicator |
| **P2.2 CRC** | `tsa_tr101290_p2_crc_errors_total` | PSI/SI CRC32 mismatches |
| **P2.3 PCR JIT** | `tsa_metrology_pcr_jitter_ms` | Current jitter (Settled) |
| **P2.4 PCR REP** | `tsa_tr101290_p2_pcr_repetition_errors_total` | Interval > 40ms |

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
| **BITRATE** | `tsa_metrology_pid_bitrate_bps` | Current instantaneous rate |
| **PEAK BR** | `tsa_metrology_pid_bitrate_peak_bps`| Session maximum |
| **AVG BR** | `tsa_metrology_pid_bitrate_avg_bps` | Session average |

---
*Note: The NOC Dashboard fetches core metrics from `/metrics` and detailed focus data from `/api/v1/snapshot`. Both are derived from the same deterministic metrology engine.*
