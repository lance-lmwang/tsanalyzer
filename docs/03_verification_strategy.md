# TsAnalyzer Pro: Professional Stability Verification Strategy

This document defines the procedures and criteria for certifying the long-term operational stability of the TsAnalyzer Pro NOC engine.

---

## 1. Test Architecture: The "Stress Triangle"

Our stability suite operates on three concurrent layers to ensure no single point of failure in the monitoring pipeline.

### Layer 1: Load Ingestion (The Heart)
- **Tool**: `build/tsp` (Transport Stream Pacer)
- **Configuration**: 16 concurrent high-definition streams (CCTV HD Sample).
- **Throughput**: 20-30 Mbps per stream (~480 Mbps total throughput).
- **Purpose**: Stress the C-engine's multi-threaded ingest and PID analysis logic.

### Layer 2: Observability Stack (The Eyes)
- **Backbone**: Prometheus (Time-series DB) + Grafana (Visual Wall).
- **NOC Standard**: Ultimate v3.5.2 Industrial Dashboard.
- **Refresh**: 3s-5s industrial cycle to ensure low QPS on backend.

### Layer 3: Automated Audit (The Brain)
- **Tools**: `scripts/self_verify_noc.py` and `scripts/verify_data_flow.py`.
- **Purpose**: Programmatic validation of data integrity (detecting nulls, label drift, or clock desync).

---

## 2. Operational Guide: How to Run Stability Tests

### Step 1: Environment Purge
Before a new benchmark, clear all historical artifacts to ensure a "clean room" baseline.
```bash
cd monitoring
./monitoring-purge.sh --all
./monitoring-up.sh
```

### Step 2: Launch the Stress Engine
Start the NOC server and orchestrate 16 streams using the professional automation script.
```bash
# Duration in seconds (e.g., 3600 for 1 hour)
nohup ./scripts/verify_pro_stability.sh 3600 > stability.log 2>&1 &
```

### Step 3: Performance & Integrity Audit
Run the self-verification suite to confirm the data pipeline is fully operational.
```bash
python3 scripts/self_verify_noc.py
```
**Success Criteria**:
- [ ] 16/16 Stream IDs detected in Prometheus.
- [ ] Non-zero values for `tsa_health_score` and `tsa_essence_video_fps`.
- [ ] PID Inventory matches the known sample stream composition.

---

## 3. Key Performance Indicators (KPIs)

| Metric | Target (16 Streams) | Critical Threshold |
| :--- | :--- | :--- |
| **CPU Usage** | < 25% (on 8-core host) | > 80% (Processing lag) |
| **Memory (RSS)** | ~30 MB (Stable) | > 100 MB (Potential Leak) |
| **Metrics Latency** | < 50ms (Cache Hit) | > 500ms (Cache Miss/Deadlock) |
| **RST Predictor** | Stable > 30s | < 10s (Buffer Warning) |

---

## 4. Advanced: Serving from Cache (v3.6+)
The `tsa_server` now utilizes an **Asynchronous Metrics Cache**.
- **Update Frequency**: 1Hz (Every 1 second).
- **Behavior**: HTTP `/metrics` requests perform a direct memory read, decoupling analysis load from monitoring load. This allows high-concurrency dashboard access without impacting real-time packet processing.

---

## 5. Troubleshooting the "No Data" Ghost
If the dashboard shows "No Data" despite the server running:
1. **Variable Lock**: Ensure the `stream_id` dropdown in Grafana has a value selected (e.g., `STR-1`).
2. **UID Sync**: Verify the Prometheus Datasource UID matches `PBFA97CFB590B2093`.
3. **Firewall**: Ensure ports `3000` and `8080` are open for physical IP access.
