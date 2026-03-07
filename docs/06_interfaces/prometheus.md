# Prometheus & Grafana Integration

TsAnalyzer exports high-density metrics via an embedded Prometheus exporter. This document defines the standard metric set for the Professional edition, organized by the 7-Layer Observability Model.

## 1. Metrics Endpoint
By default, the appliance serves metrics on `http://<host>:9090/metrics` (or port `12345` for `tsa_cli`).

## 2. Metric Registry

### 2.1 System & Engine Health (`tsa_system_`)
| Metric Name | Type | Description |
| :--- | :--- | :--- |
| `tsa_system_signal_locked` | Gauge | 1 if the stream is locked and analysis is active, 0 otherwise. |
| `tsa_system_health_score` | Gauge | 0-100 composite health score. |
| `tsa_system_internal_drop_count` | Counter | Total packets dropped by the analyzer due to CPU/Memory saturation. |
| `tsa_system_worker_overruns` | Counter | Total slice overruns in the high-priority processing threads. |
| `tsa_system_engine_latency_ns` | Gauge | Current processing latency per packet in nanoseconds. |
| `tsa_system_global_incident` | Gauge | 1 if more than 50% of monitored streams are in a failed state. |

### 2.2 Transport & Link Layer (`tsa_transport_`)
| Metric Name | Type | Description |
| :--- | :--- | :--- |
| `tsa_transport_srt_rtt_ms` | Gauge | SRT Round Trip Time in milliseconds. |
| `tsa_transport_srt_retransmit_rate` | Gauge | Ratio of retransmitted packets to total packets. |
| `tsa_transport_mdi_mlr_pkts_s` | Gauge | MDI Media Loss Rate (packets/second). |
| `tsa_transport_mdi_df_ms` | Gauge | MDI Delay Factor (network jitter estimate in ms). |
| `tsa_transport_iat_histogram` | Histogram | Inter-Arrival Time distribution buckets (1ms to 100ms). |

### 2.3 TR 101 290 Compliance (`tsa_compliance_`)
| Metric Name | Type | Description |
| :--- | :--- | :--- |
| `tsa_compliance_tr101290_p1_sync_loss_total` | Counter | Sync Loss (P1.1). |
| `tsa_compliance_tr101290_p1_pat_errors_total` | Counter | PAT Error (P1.3). |
| `tsa_compliance_tr101290_p1_pmt_errors_total` | Counter | PMT Error (P1.5). |
| `tsa_compliance_tr101290_p1_cc_errors_total` | Counter | Continuity Counter Error (P1.4). |
| `tsa_compliance_tr101290_p1_pid_errors_total` | Counter | PID Error (P1.6). |
| `tsa_compliance_tr101290_p2_pts_errors_total` | Counter | PTS Repetition Error (P2.5). |
| `tsa_compliance_tr101290_p2_crc_errors_total` | Counter | SI Section CRC Error (P2.2). |
| `tsa_compliance_tr101290_p2_transport_errors_total`| Counter | Transport Error Indicator (P2.1). |
| `tsa_compliance_tr101290_p3_sdt_errors_total` | Counter | SDT repetition interval > 2s (P3.2). |
| `tsa_compliance_tr101290_p3_nit_errors_total` | Counter | NIT repetition interval > 10s (P3.1). |
| `tsa_compliance_pcr_repetition_errors` | Counter | PCR Repetition interval > 40ms. |
| `tsa_compliance_pcr_accuracy_errors` | Counter | PCR Accuracy > ±500ns. |

### 2.4 Metrology & Timing (`tsa_metrology_`)
| Metric Name | Type | Description |
| :--- | :--- | :--- |
| `tsa_metrology_physical_bitrate_bps` | Gauge | Raw bitrate at the physical input layer. |
| `tsa_metrology_pcr_bitrate_bps` | Gauge | Precision bitrate derived from 27MHz Software PLL. |
| `tsa_metrology_pcr_bitrate_piecewise_bps` | Gauge | Bitrate calculated between the last two PCRs. |
| `tsa_metrology_pcr_jitter_ms` | Gauge | Alpha-Beta filtered PCR Jitter. |
| `tsa_metrology_pcr_accuracy_ns` | Gauge | Real-time PCR Accuracy deviation. |
| `tsa_metrology_stc_wall_drift_ppm` | Gauge | Clock drift between 27MHz STC and System Clock. |

### 2.5 Content Quality (`tsa_essence_`)
| Metric Name | Type | Description |
| :--- | :--- | :--- |
| `tsa_essence_video_fps` | Gauge | Detected video frame rate. |
| `tsa_essence_gop_ms` | Gauge | Group of Pictures duration in milliseconds. |
| `tsa_essence_av_sync_ms` | Gauge | Audio/Video synchronization offset. |
| `tsa_essence_entropy_freeze_total` | Counter | Total freeze/black frames detected via entropy analysis. |

### 2.6 Predictive Simulation (`tsa_predictive_`)
| Metric Name | Type | Description |
| :--- | :--- | :--- |
| `tsa_predictive_rst_network_seconds` | Gauge | **Remaining Safe Time**: Predicted seconds until buffer underflow. |
| `tsa_predictive_tstd_underflow_total` | Counter | Total T-STD Buffer Underflow events simulated. |
| `tsa_predictive_tstd_overflow_total` | Counter | Total T-STD Buffer Overflow events simulated. |

---

### 2.7 PID-Level Metrics
These metrics are labeled by `stream_id`, `pid`, `type`, and `codec` for granular drill-down.

| Metric Name | Type | Category | Description |
| :--- | :--- | :--- | :--- |
| `tsa_metrology_pid_bitrate_bps` | Gauge | Metrology | Instantaneous bitrate of this PID. |
| `tsa_compliance_pid_tstd_eb_fill_pct` | Gauge | Compliance | T-STD Elementary Buffer (EB) occupancy. |
| `tsa_compliance_pid_has_scte35` | Gauge | Compliance | 1 if SCTE-35 signaling is present on this PID. |
| `tsa_essence_pid_video_width` | Gauge | Essence | Decoded video width. |
| `tsa_essence_pid_video_height` | Gauge | Essence | Decoded video height. |
| `tsa_essence_pid_video_profile` | Gauge | Essence | Video Profile IDC. |
| `tsa_essence_pid_video_level` | Gauge | Essence | Video Level IDC. |
| `tsa_essence_pid_video_bit_depth` | Gauge | Essence | Bit depth (e.g. 8, 10). |
| `tsa_essence_pid_video_chroma_format` | Gauge | Essence | Chroma format (1=4:2:0, 2=4:2:2). |
| `tsa_essence_pid_video_gop_n` | Gauge | Essence | Group of Pictures (GOP) frame count. |
| `tsa_essence_pid_closed_gops_total` | Counter | Essence | Total number of Closed GOPs detected. |
| `tsa_essence_pid_open_gops_total` | Counter | Essence | Total number of Open GOPs detected. |

---

## 4. Alerting via PromQL

**Critical Service Impact (Signal Locked but Buffer Depleting):**
```promql
tsa_predictive_rst_network_seconds < 2.0 and tsa_system_signal_locked == 1
```

**Content Freeze (Detected via Entropy):**
```promql
rate(tsa_essence_entropy_freeze_total[1m]) > 0
```
