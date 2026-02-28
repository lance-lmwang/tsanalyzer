# Technical Spec: State & History Engine (Layer 5)

The State & History Engine is responsible for temporal data management, providing context to instantaneous metrics through rolling windows and long-term historical aggregation. It serves as the primary data source for SLA auditing and trend analysis.

---

## 1. Multi-Tier Temporal Data Model

To balance high-resolution monitoring with storage efficiency, data is managed in three distinct tiers.

### Tier 1: Instantaneous Snapshots (100ms)
- **Source**: Direct from Layer 3 (Analysis Core).
- **Metric Types**: Current Bitrate, Current PCR Jitter, Active P1 Error Flags.
- **Retention**: In-memory only (Volatile).

### Tier 2: Rolling Window Statistics (1s, 10s, 60s)
- **Source**: Aggregated from Tier 1.
- **Metrics**: `avg`, `min`, `max`, and `p99` for all primary indicators.
- **Retention**: In-memory circular buffers (Fixed size).
- **Purpose**: Detecting bursty network behavior and intermittent encoder drift.

### Tier 3: Historical Aggregates (1h, 24h, 30d)
- **Source**: Compacted from Tier 2.
- **Metrics**: Hourly averages, daily extrema, and **Cumulative Error Durations**.
- **Retention**: Persistent storage (JSON-lines or SQLite).
- **Purpose**: Compliance reporting and long-term stability auditing.

---

## 2. SLA & Availability Calculation

The engine calculates the **Service Level Agreement (SLA)** based on the duration of "Impaired" states provided by Layer 4 (Alarm Lifecycle Engine).

### 2.1 Availability Formula
Availability is defined as the percentage of time the service spent without **ACTIVE Critical (P1)** faults.

$$Availability \% = \frac{T_{total} - \sum T_{active\_P1\_alarms}}{T_{total}} 	imes 100$$

### 2.2 Calculation Logic:
- **T_total**: Total wall-clock time in the measurement window.
- **Duration Tracking**: Layer 5 tracks the `clear_time - active_time` delta for every alarm.
- **Overlapping Alarms**: If multiple P1 alarms are active simultaneously (e.g., PAT_error and SYNC_error), the duration is only counted once for the SLA calculation.

---

## 3. Data Compression & Compaction

To maintain a constant disk footprint for the Appliance Mode, Layer 5 implements a **Temporal Compaction** strategy:
1.  **Level 1**: Per-second metrics are averaged into **1-minute** bins after 1 hour.
2.  **Level 2**: 1-minute bins are compacted into **1-hour** summary blocks (Min/Max/Avg) after 24 hours.
3.  **Result**: 30 days of high-level history for 128 streams fits within ~5GB of storage.

---

## 4. Deterministic History Reconstruction

To support forensic re-analysis of PCAP files:
- **Replay Mode**: When replaying a file, Layer 5 uses the **reconstructed 27MHz STC** as the timebase instead of system time.
- **Consistency**: This ensures that re-analyzing a file 100 times yields identical "Historical Trends" and SLA results, a critical requirement for legal disputes.

---

## 5. Industrial Metrics Schema (`tsa_history_t`)

```json
{
  "stream_id": "STR-HD-01",
  "window_start": "2026-02-28T09:00:00Z",
  "window_duration_sec": 3600,
  "metrics": {
    "bitrate": { "avg": 8500200, "min": 8100000, "max": 8900000 },
    "pcr_jitter": { "avg_ns": 120, "max_ns": 480, "p99_ns": 450 },
    "mdi_df": { "max_ms": 12.5 }
  },
  "sla": {
    "availability_pct": 99.985,
    "total_downtime_sec": 0.54,
    "p1_alarm_count": 2
  },
  "engine_metadata": {
    "engine_version": "2.1.0",
    "metrics_schema_id": 42
  }
}
```

---

## 6. Integration with Presentation (Layer 6)

- **Query Interface**: Layer 6 requests historical data via `GET /api/v1/history?stream_id=X&range=24h`.
- **Prometheus Export**: Layer 5 pushes daily SLA and rolling window extrema to the Prometheus Exporter to populate long-term Grafana dashboards.
