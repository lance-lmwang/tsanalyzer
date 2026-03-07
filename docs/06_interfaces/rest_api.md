# REST API Reference

TsAnalyzer Server provides a high-performance REST API for dynamic stream management and real-time telemetry extraction.

---

## 1. Stream Management

### 1.1 Create/Update a Stream
`POST /api/v1/streams`

Creates a new analysis pipeline. The payload follows the hierarchical configuration model.

**Payload Example:**
```json
{
  "id": "tenant-a/live/ch1",
  "metrology": {
    "pcr_jitter": true,
    "hls_audit": false
  },
  "compliance": {
    "tr101290": true
  },
  "pipeline": {
    "enabled": true,
    "bitrate": "15Mbps",
    "outputs": ["udp://239.1.1.1:1234"]
  },
  "alert": {
    "webhook_url": "http://alert-manager/notify",
    "filter_mask": "0x0F"
  }
}
```

### 1.2 Delete a Stream
`DELETE /api/v1/streams/{id}`

---

## 2. Telemetry & Monitoring

### 2.1 Get Global Metrics
`GET /metrics`

Returns high-density Prometheus-compatible metrics. See [Observability Model](../04_operations/observability_model.md) for details.

### 2.2 Get Real-time Snapshot
`GET /api/v1/snapshot`

Returns a full JSON snapshot of all active streams, including TR 101 290 status and PCR jitter values.

### 2.3 System Health
`GET /health`

Lightweight endpoint for load-balancer and Kubernetes liveness probes. Returns `200 OK`.

---

## 3. Configuration Management

### 3.1 Hot-Reload
`POST /api/v1/config/reload`

Triggers a zero-latency configuration reload from the local `tsa.conf`.
