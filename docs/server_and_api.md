# TsAnalyzer: Server, API & Visual Interfaces

This document defines how TsAnalyzer interacts with the external world through high-performance server architectures, automated APIs, and professional monitoring surfaces.

---

## 1. High-Performance Server Architecture (The Side-Car Model)

`tsa_server_pro` is an **Inline Deterministic Inspection Engine**. It simultaneously performs ultra-stable stream forwarding and CPU-bound deep inspection.

### 1.1 Dual-Path Paradigm
To ensure forwarding stability, the engine uses a decoupled observer model:
*   **Forward Path (Primary)**: Handles packet routing and socket writes in strict $O(1)$ time. Immune to analysis load.
*   **Analysis Path (Side-Car)**: Inspection workers process mirrored copies of traffic via hardware-optimized SPSC ring buffers.

### 1.2 Bare-Metal Implementation Principles
*   **NUMA-Local Sharding**: I/O threads, workers, and buffers are pinned to the same NUMA node to eliminate L3 cache miss storms.
*   **Hardware Timestamping**: Uses `SO_TIMESTAMPING` to extract PHY-level nanosecond arrival times, bypassing OS scheduler jitter.
*   **TSC Fairness Quota**: Workers use the CPU's Time Stamp Counter (`rdtsc`) to enforce microsecond execution bounds, ensuring large streams don't stall smaller ones.

---

## 2. Integrated Automation API (REST/gRPC)

TsAnalyzer Pro follows an **API-First** architecture. The default port is `9090`.

### 2.1 Stream Management
*   `POST /api/v1/streams`: Register a new monitoring task (UDP/SRT).
*   `DELETE /api/v1/streams/:id`: Stop and free resources.
*   `GET /config/streams`: List active tasks.

### 2.2 Telemetry & Metrology
*   `GET /snapshot`: Returns real-time JSON metrology.
*   `GET /streams/:id/metrics`: Prometheus-compatible metrics.

### 2.3 Success Snapshot Example
```json
{
  "health": 98.5,
  "p1_alarms": { "cc_error": { "count": 0, "ts": 0 } },
  "metrics": {
    "bitrate_bps": 10000000,
    "pcr_jitter_ns": 120.5,
    "mdi_df_ms": 5.24,
    "video_metadata": { "width": 1920, "height": 1080, "gop_n": 30 }
  }
}
```

---

## 3. Operational Surface: The Three-Plane Model

To manage high-density fleets, the NOC UI is divided into three isolated functional planes.

### 3.1 Plane 1: Global Mosaic Wall
A situational awareness field using a dynamic packing algorithm.
*   **States**: 🟢 Stable, 🟡 Degraded, 🔴 Critical, ⚫ No Signal, 🔵 Engine Drop.

### 3.2 Plane 2: Stream Focus View (7-Tier Grid)
A diagnostic dashboard optimized for 4K UHD eye-scan:
1.  **SIGNAL STATUS**: Master fidelity and locking status.
2.  **LINK INTEGRITY**: SRT RTT and MDI-DF matrix.
3.  **ETR P1**: TR 101 290 Priority 1 real-time errors.
4.  **CLOCK & TIMING**: PCR accuracy and repetition.
5.  **MUX PAYLOAD**: PID bitrate distribution and null density.
6.  **ESSENCE**: GOP cadence, FPS stability, and AV-Sync.
7.  **FORENSIC**: Audit trail and millisecond-accurate event log.

### 3.3 Plane 3: Forensic Replay
A bit-exact evidence audit layer using the server's **Triggered Micro-Capture** capability.

---

## 4. Visual Forensics & Active Signaling

### 4.1 Sparse Thumbnail Generation
*   **Action**: When the NALU sniffer identifies an IDR frame, a low-priority background thread (utilizing `libavcodec`) decodes one frame every 10 seconds.
*   **Result**: Provides a visual "peek" into the stream for remote NOC verification without impacting line-rate metrology.

### 4.2 Webhook Signaling Engine
*   **Anti-Storm Dispatcher**: Critical P1 events trigger immediate sub-second JSON POST notifications to a configured URL.
*   **Benefit**: Reduces alarm latency from the 15s Prometheus pull interval to < 500ms.
