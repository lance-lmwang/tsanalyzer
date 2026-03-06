# TsAnalyzer Appliance: Server & API Architecture

This document defines how the TsAnalyzer Appliance manages high-density stream analysis and exposes its intelligence via modern interfaces.

---

## 1. High-Performance Server Model (Side-Car Architecture)

TsAnalyzer Appliance employs a **Side-Car Inspection** model to ensure that deep packet inspection never interferes with the primary stream delivery path.

### 1.1 Decoupled Data Plane
*   **Primary Path**: Handles raw stream ingest and (if enabled) low-latency forwarding/pacing.
*   **Observer Path**: A bit-exact copy of every packet is pushed to the **Metrology Worker** via NUMA-local SPSC rings.
*   **Isolation**: If the metrology core experiences a CPU spike (e.g., during complex SCTE-35 auditing), the delivery path remains unaffected.

---

## 2. Asynchronous Audit Pipelines

TsAnalyzer v3 offloads compute-intensive content auditing to specialized asynchronous queues to maintain deterministic performance.

### 2.1 Sidecar Workers & Queues
To maintain 10Gbps line-rate metrology, the engine uses a multi-queue handoff system:
*   **Thumbnail Queue**: Background workers (libavcodec) consume IDR frames to generate periodic thumbnails.
*   **Audio Audit Queue**: Specialized workers calculate LUFS loudness and detect silence/clipping.
*   **Signaling Queue**: Handles non-blocking Webhook dispatch and upstream management signaling.

---

## 3. Webhook Signaling Engine (Active Alerts)

Proactive notification is critical for sub-second incident response.

### 3.1 Signaling Mechanism
1.  **Detection**: The Metrology Worker identifies an incident (e.g., Sync Loss).
2.  **Handoff**: Pushes a structured event to the **Signaling Queue**.
3.  **Dispatch**: A dedicated `signal_thread` (non-blocking) executes HTTP POSTs to configured Webhook URLs.
4.  **Anti-Storm**: Implements alarm dampening to prevent redundant alerts during network flaps.

---

## 4. REST & Prometheus Interfaces

The Appliance is **API-First**. All dashboards and management tools consume the same public endpoints.

### 4.1 Active Task Orchestration
Unlike passive monitors, the Appliance manages the full lifecycle of monitoring tasks:
*   **Dynamic Provisioning**: APIs to spawn/kill engine instances based on scheduled broadcast events.
*   **Resource Balancing**: Automatically assigns monitoring tasks to specific NUMA nodes and CPU cores based on aggregate PPS load.

### 4.2 Multiplex & Service Grouping
Metrics are aggregated at multiple logical levels:
*   **Physical Level**: Raw NIC and IP stats.
*   **Multiplex Level**: Aggregated health of all programs within a single TS/MPTS.
*   **Service Level**: End-to-end health of a specific logical channel across multiple delivery paths (e.g., comparing Main vs. Backup variant).

### 4.3 Metrics API (Prometheus)
Native exporter for high-density time-series data.
*   **Target Interval**: 1s - 15s.
*   **Labels**: Every metric is tagged with `stream_id`, `multiplex_id`, and `node_id`.
