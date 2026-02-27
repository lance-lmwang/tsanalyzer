# API Design: TsAnalyzer Pro Integration Interface

TsAnalyzer Pro follows an **API-First** architecture. The embedded Web UI is a consumer of these APIs, ensuring that any action performed in the UI can be fully automated by customers.

## 1. Global Standards
- **TsAnalyzer API Port**: `8080` (Default)
- **Grafana UI Port**: `3000` (Standard)
- **Base URL**: `http://<device-ip>:8080/api/v1`
- **Content-Type**: `application/json`

## 2. Telemetry API (Data Plane)

### `GET /snapshot`
Returns real-time metrology for the currently selected or primary stream.
- **Success (200)**: Returns the snapshot JSON (as defined in v2.1 spec).
- **Error (503)**: Engine warming up or no stream active.

### `GET /streams/:id/metrics`
Returns Prometheus-compatible plain text metrics for a specific stream.

## 3. Prometheus Metric Alignment (Grafana Parity)

To ensure the Grafana dashboard mirrors the Standalone UI exactly, the exporter MUST use the following metric naming convention:

| Dashboard Component | Prometheus Metric Name | Labels |
| :--- | :--- | :--- |
| **Master Health** | `tsa_health_score` | `stream_id` |
| **Signal Lock** | `tsa_signal_lock_status` | `stream_id` |
| **SRT RTT** | `tsa_srt_rtt_ms` | `stream_id` |
| **SRT Loss** | `tsa_srt_unrecovered_loss_total` | `stream_id` |
| **TR 101 290 P1** | `tsa_tr101290_p1_errors` | `stream_id, error_type` |
| **ES FPS** | `tsa_essence_video_fps` | `stream_id` |
| **ES AV Sync** | `tsa_essence_av_sync_ms` | `stream_id` |
| **MDI-DF** | `tsa_mdi_delay_factor_ms` | `stream_id` |
| **PID Bitrate** | `tsa_pid_bitrate_bps` | `stream_id, pid, type` |

## 4. Control Plane API (Stream Management)

### `GET /config/streams`
List all analysis tasks.

### `POST /config/streams`
Starts a new analysis task.
**Payload:**
```json
{
  "stream_id": "HK-DIST-01",
  "url": "srt://0.0.0.0:9001?mode=listener",
  "type": "SRT",
  "latency_ms": 250
}
```

### `DELETE /config/streams/:id`
Stops and deletes an analysis task.

## 5. System & Diagnostics
### `GET /health`
Returns system vitals (CPU, Mem, Uptime, Engine Status).

### `POST /sys/reboot`
Restarts the analyzer process.

---

## 6. Integration Example (cURL)
```bash
# Add a new SRT stream for monitoring
curl -X POST http://192.168.7.2:8080/api/v1/config/streams \
     -H "Content-Type: application/json" \
     -d '{"stream_id": "HK-PROD", "url": "srt://0.0.0.0:9001", "latency_ms": 400}'

# Delete a stream and free resources
curl -X DELETE http://192.168.7.2:8080/api/v1/config/streams/HK-PROD
```

## 7. Static Assets
- `GET /`: Serves the `index.html` (NOC Dashboard + Config UI).
- `GET /health`: Returns `200 OK`.
