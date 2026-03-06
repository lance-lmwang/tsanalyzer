# Prometheus & Grafana Integration

TsAnalyzer Appliance exports high-density metrics via an embedded Prometheus exporter.

## 1. Metrics Endpoint
By default, the appliance serves metrics on `http://<host>:9090/metrics` (or port `12345` for `tsa_cli`).

## 2. Core Metric Registry

| Metric Name | Type | Description |
| :--- | :--- | :--- |
| `tsa_signal_fidelity` | Gauge | 0-100 score of signal health. |
| `tsa_pcr_jitter_ns` | Gauge | Real-time jitter vs Software PLL. |
| `tsa_mdi_df_ms` | Gauge | MDI Delay Factor (Network Jitter). |
| `tsa_stc_wall_drift_ppm`| Gauge | Long-term clock drift in ppm. |
| `tsa_buffer_safety_pct`| Gauge | Distance to underflow (0-100%). |
| `tsa_tr101290_errors` | Counter | Labeled by `error_type` (p1_sync, cc, etc). |

---

## 3. High-Density NOC Visualization

TsAnalyzer provides pre-configured dashboards following the **Three-Plane Architecture**:

1.  **Global Wall (Plane 1)**: Visual status of all streams in a grid.
2.  **Diagnostic View (Plane 2)**: 7-Tier grid for a single stream focus.
3.  **Forensic Trail (Plane 3)**: Zoomable timeline of historical incidents.

## 4. Alerting via PromQL
Recommended alerting rule for critical service impact:
```promql
tsa_buffer_safety_pct < 20 and tsa_signal_fidelity < 90
```
This rule ignores transient jitter but fires if both the buffer is depleting and structural errors are increasing.
