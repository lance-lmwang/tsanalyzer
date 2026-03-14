# Spec: Professional Metrology Monitoring Matrix (v3.0)

## 1. Objective
Elevate `tsa_server_pro` observability to match high-end hardware analyzers (Tektronix MTS4000). The focus is on high-resolution distribution data, per-stream resource accounting, and industrial transport statistics.

## 2. Success Criteria
*   **PCR Accuracy Histograms**: Lock-free distribution bins for jitter tracking.
*   **Resource Accounting**: CPU cycles and memory footprint reported per `stream_id`.
*   **SRT Deep Stats**: Integration of SRT-native RTT, congestion, and retransmit metrics.
*   **Performance Zero-Impact**: Metrics collection must use atomic operations; no locks in the data path.
*   **Compliance**: 100% alignment with Prometheus best practices and TR 101 290 nomenclature.

## 3. Technical Requirements
### 3.1 High-Resolution Histograms
*   **IAT Buckets**: 100us, 500us, 1ms, 2ms, 5ms, 10ms, 50ms, 100ms.
*   **PCR Jitter Buckets**: 500ns, 1us, 5us, 10us, 100us, 1ms, 10ms, >50ms.

### 3.2 Engine Self-Telemetry
*   **Per-Handle Cycles**: Capture CPU time spent in `tsa_feed_data` per stream.
*   **Pipeline Latency**: Measure nanoseconds from packet ingress to snapshot commit (P99).

### 3.3 Multi-Tenant Metrics
*   All metrics must be labeled with `stream_id` and `node_id`.
