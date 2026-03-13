# TsAnalyzer REST API: v3 Implementation

The TsAnalyzer Pro Control Plane provides a high-performance REST API for stream management, real-time telemetry, and forensic data access.

---

## 1. Security & Authentication

TsAnalyzer utilizes a **Two-Tier Security Gateway** to protect the control plane.

### 1.1 JWT Authentication
All requests to `/api/v1/*` must include a valid JSON Web Token (JWT) in the header.
*   **Header**: `Authorization: Bearer <TOKEN>` or `X-TSA-Token: <SECRET>`
*   **Default Behavior**: In production, anonymous access returns `401 Unauthorized`.

### 1.2 Rate Limiting (Token Bucket)
To prevent API denial-of-service, a global token bucket is applied per remote IP.
*   **Default Capacity**: 100 requests
*   **Refill Rate**: 10 tokens/sec
*   **Exceeded**: Returns `429 Too Many Requests`.

---

## 2. Global Stream Snapshots

### 2.1 Get All Active Streams
Returns a concise summary of every stream currently being processed by the engine.

*   **Endpoint**: `GET /api/v1/snapshot`
*   **Sample Command**:
```bash
curl -s -H "Authorization: Bearer my-secret-key" http://localhost:8080/api/v1/snapshot | jq
```
*   **Response (JSON)**:
```json
{
  "active_streams": 2,
  "system_health": 98.5,
  "streams": [
    {
      "id": "e2e_stream_1",
      "status": "VALID",
      "bitrate_mbps": 12.45,
      "health_score": 100.0,
      "active_alerts": 0
    }
  ]
}
```

---

## 3. Deep Metrology Detail

### 3.1 Get Specific Stream Detail
Provides 3D PCR jitter, T-STD buffer levels, and ES-layer metadata.

*   **Endpoint**: `GET /api/v1/snapshot/<stream_id>`
*   **Sample Response**:
```json
{
  "id": "main_hd_channel",
  "metrology": {
    "pcr_jitter_ns": 4500,
    "pcr_drift_ppm": 0.12,
    "mdi_df_ms": 1.2,
    "tstd_eb_fill": 0.65
  },
  "essence": {
    "video_res": "1920x1080",
    "gop_duration_ms": 500,
    "has_cc": true,
    "last_scte35_event_id": "0xDEADC0DE"
  }
}
```

---

## 4. Operational Control

### 4.1 Delete Stream
Instantly terminates a processing node and releases associated network resources.

*   **Endpoint**: `DELETE /api/v1/snapshot/<stream_id>`
*   **Response**: `{"status": "ok"}`
