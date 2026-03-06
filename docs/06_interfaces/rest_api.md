# REST API Reference

The TsAnalyzer Appliance follows an **API-First** design. The Web UI is a consumer of these documented endpoints.

## 1. Stream Management

### Register a Stream
`POST /api/v1/streams`
```json
{
  "stream_id": "CH-01",
  "url": "srt://47.92.1.1:9000?mode=caller",
  "config": { "enable_pacing": true }
}
```

### Remove a Stream
`DELETE /api/v1/streams/:id`

---

## 2. Metrology Snapshots

### Instantaneous Snapshot
`GET /api/v1/snapshot/:id`
Returns the current Tier 1-6 state including video metadata and active alarms.

### Full Engine State
`GET /api/v1/metrology/full`
Returns a global JSON object containing every monitored stream and the Master Health of the appliance.

---

## 3. Forensic Export

### Download Capture
`GET /api/v1/forensics/:id/capture`
Downloads the last 500ms of raw TS packets associated with the most recent Priority 1 incident.
