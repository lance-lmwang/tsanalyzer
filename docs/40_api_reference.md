# API Reference: Smart Gateway Automation

The suite provides a dual-layer API: a high-performance **C Library** for core logic and a **REST/gRPC Automation API** for fleet orchestration.

---

## 1. Automation API (REST/gRPC)

### 1.1 `POST /api/v1/streams`
Registers a monitoring and relay task.
- **Payload Parameters**:
    - `tenant_id`: Mandatory for SaaS-grade isolation.
    - `visibility_mode`: `BLIND` (SRT Only), `SEMI-BLIND` (P1/P2), or `FULL` (ABR Audit).
    - `pacing_enabled`: Enables TsPacer repair before relay.
    - `fail_strategy`: `transparent_pass_through` (L4 Bypass) or `block`.

### 1.2 `GET /api/v1/streams/{id}`
Retrieves atomic snapshots of quantifiable metrics (RST, MDI, T-STD) and SLA performance.

---

## 2. Core C Library API

**Library Prefixes**: `tsa_` (Analyzer), `tsp_` (Pacer)

### 2.1 `tsa_handle_t* tsa_create(const tsa_config_t* cfg)`
Initializes the Brain.
- **Profile Support**: `TSA_PROFILE_STRICT_SYNC` or `TSA_PROFILE_RESILIENT_WAN`.
- **Memory Contract**: Pre-allocates zero-copy pools. No Edition-based limits; visibility is controlled by stream keys.

### 2.2 `tsa_status_t tsa_process_packet(tsa_handle_t* h, const uint8_t* pkt, uint64_t now)`
Wait-free analysis entry point. Supports automated MPTS/SPTS re-sync.

### 2.3 `tsp_handle_t* tsp_create(const tsp_config_t* cfg)`
Initializes the Muscle for pacing. Integrated with the server egress dispatcher.

---

## 3. Metrology JSON Snapshot (`GET /api/v1/metrology/full`)

TsAnalyzer 2.0 provides a structured, broadcast-grade JSON report for live streams.

### 3.1 Response Structure
```json
{
  "status": "ok",
  "health": 45.0,
  "signal_lock": true,
  "p1_alarms": {
    "cc_error": { "count": 12, "ts": 1772206774646, "msg": "CC mismatch on PID 256: expected 4, found 6" },
    "pat_error": { "count": 0, "ts": 0, "msg": "" },
    ...
  },
  "p2_alarms": {
    "pcr_accuracy": { "count": 134, "ts": 1772206774644, "msg": "PCR Jitter 540.2 ns exceeds ±500ns" },
    "crc_error": { "count": 5, "ts": 1772206774624, "msg": "CRC32 failed for PAT on PID 0" },
    ...
  },
  "metrics": {
    "bitrate_bps": 10000000,
    "pcr_jitter_ns": 420.5,
    "pcr_accuracy_piecewise_ms": 0.05,
    "piecewise_pcr_bitrate_bps": 9985420,
    "pcr_drift_ppm": 15.2,
    "mdi_df_ms": 5.24
  }
}
```

### 3.2 Metric Definitions
- **`health`**: 0-100 score. P1 errors trigger a 40-point penalty and a 60-point "Lid" rule.
- **`pcr_accuracy_piecewise_ms`**: Self-clocking PCR accuracy derived from the Piecewise Constant Bitrate model (PCBR). Ideal for file analysis and non-live forensics.
- **`piecewise_pcr_bitrate_bps`**: The instantaneous transport rate calculated between the last two PCR points.
- **`pcr_drift_ppm`**: Clock frequency offset in Parts Per Million. Values > ±30 indicate source clock instability.
- **`mdi_df_ms`**: Delay Factor. Buffer size (ms) required to neutralize network jitter.

---

## 4. Engineering Implementation Contract

1. **Wait-Free SPSC**: All thread handoffs MUST be wait-free.
2. **Watchdog Integrity**: The server MUST monitor analysis thread heartbeats to trigger L4 Bypass in < 10ms.
3. **128-bit Monotonicity**: Mandatory for all timing deltas using `int128_t` nanoseconds.
