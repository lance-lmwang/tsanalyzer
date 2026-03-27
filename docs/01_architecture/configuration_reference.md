# TsAnalyzer Configuration Reference

This document is the authoritative guide for the TsAnalyzer hierarchical configuration system.

## 1. Global Directives
Process-level settings.

| Directive | Default | Description |
| :--- | :--- | :--- |
| `http_listen` | `8081` | Port for REST API, Prometheus, and /health probes. |
| `srt_listen` | `9000` | Unified SRT ingress port for ID-based routing. |
| `worker_threads`| `auto` | Number of affinity-bound analysis threads. |
| `log_format` | `text` | Output format: `text` | `json`. |

## 2. Observability & Dashboard
Defines the management of real-time monitoring interfaces.

### 2.1 `dashboard` Block
- `enabled`: `on` | `off` (Controls the built-in HTML NOC dashboard).
- `refresh_interval`: `1s` | `500ms` (Browser polling suggestion).
- `static_root`: `./` (Path to `big_screen_noc.html`).

### 2.2 `telemetry` Block
- `shm_enabled`: `on` | `off` (Must be 'on' for `tsa_top` to work via Shared Memory).
- `shm_key`: `0x545341` (System V IPC key for memory segment).
- `prometheus_enabled`: `on` | `off` (Enable the `/metrics` endpoint).
- `api_detailed_snapshots`: `on` | `off` (Include per-PID stats in the global snapshot).

## 3. Stream Configuration (`stream`)
Defines a processing pipeline for a specific media source.

### 3.1 Basic Attributes
- `input`: Ingest URI (`udp://`, `srt://`, `http://`, `zixi://`, `rist://`).
- `label`: Human-readable description.
- `program_number`: (Optional) Specific program ID to analyze in an MPTS stream.

### 3.2 `metrology` Block (Timing & Bitrate)
- `pcr_jitter`: `on` | `off`
- `drift_analysis`: `on` | `off`
- `hls_audit`: `on` | `off` (Valid for HLS inputs)

### 3.3 `compliance` Block (Standards)
- `tr101290`: `on` | `off`
- `mpts_check`: `on` | `off` (Valid for MPTS)

### 3.4 `qoe` Block (Sensory Quality)
- `black_detect`: `on` | `off`
- `freeze_detect`: `on` | `off`
- `av_sync_audit`: `on` | `off`
- `entropy_window`: `1000` (Packets for entropy calculation)

### 3.5 `essence` Block (Payload)
- `codec_sniff`: `on` | `off`
- `scte35`: `on` | `off`
- `cc_audit`: `on` | `off` (CEA-608/708)

### 3.6 `pipeline` Block (Transformation & Output)
Replaces the deprecated `gateway` block.

- `enabled`: `on` | `off`
- `pacing`: `basic` | `pcr`
- `bitrate`: Target CBR (e.g., `15Mbps`)
- `outputs`: List of URIs (e.g., `[udp://239.1.1.1:1234, srt://:9001]`)
- `repair_cc`: `on` | `off`
- `repair_pcr`: `on` | `off`

### 3.7 `alert` Block (Notifications)
- `webhook_url`: HTTP endpoint.
- `filter`: Array of metrics (e.g., `[P1.1, P1.4, FAILOVER]`).
- `cooldown`: Summary period (e.g., `10s`).

## 4. Inheritance Example

```nginx
# Global Settings
http_listen 8088;
log_format json;

# Observability Setup
dashboard {
    enabled on;
    refresh_interval 1s;
}

telemetry {
    shm_enabled on;
    prometheus_enabled on;
}

# Baseline Template
vhost __default__ {
    metrology { pcr_jitter on; }
    compliance { tr101290 on; }
    alert { webhook_url http://central-logs/api; }
}

# Multi-tenant Stream with Pipeline
stream tenant-a/live/ch1 {
    input srt://tenant-a/ch1; # Matched via StreamID

    # Custom QoE for high-value stream
    qoe {
        freeze_detect on;
        av_sync_audit on;
    }

    # Pipeline: Single Ingest -> Dual Egress
    pipeline {
        enabled on;
        bitrate 10Mbps;
        outputs [
            udp://239.1.1.1:1234,
            srt://:9001?streamid=output_handoff
        ];
    }
}
```
