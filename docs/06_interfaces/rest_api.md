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

## 4. Security & Authentication

TsAnalyzer Pro implements a multi-layer security model to protect telemetry and stream integrity.

### 4.1 API Authentication (Bearer Token)
All administrative endpoints (POST/DELETE) require a valid **Bearer Token** in the `Authorization` header.

*   **Mechanism**: Fixed-token authentication for standalone probes or OIDC integration for Appliance deployments.
*   **Header**: `Authorization: Bearer <API_KEY>`

### 4.2 Transport Layer Security (HTTPS)
Production deployments MUST enable TLS 1.3 for all REST API communication.
*   **Certificates**: Supports PEM-encoded X.509 certificates and keys.
*   **Self-Signed**: For laboratory use, can be overridden via `TLS_INSECURE_SKIP_VERIFY`.

### 4.3 Data Ingest Security (SRT AES)
For SRT ingest, TsAnalyzer Pro supports industry-standard AES encryption.
*   **Encryption Modes**: AES-128, AES-192, and AES-256.
*   **Passphrase**: Defined in the stream configuration:
    ```json
    {
      "ingest": {
        "url": "srt://:9000?passphrase=secret-key&pbkeylen=32"
      }
    }
    ```

### 4.4 Multi-Tenancy Isolation
In the Appliance profile, streams are partitioned into **Tenants**.
*   **Namespace**: IDs are prefixed with the tenant name (e.g., `tenant-a/stream-1`).
*   **Access Control**: API tokens are scoped to specific tenants, preventing cross-tenant telemetry leakage.
