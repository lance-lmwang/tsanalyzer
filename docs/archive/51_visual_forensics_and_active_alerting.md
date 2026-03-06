# TsAnalyzer: Visual Forensics & Active Alerting Design

## 1. Overview
To achieve complete industrial parity with high-end monitoring solutions (inspired by `opensrthub` and professional probes), TsAnalyzer must move beyond numerical metrics and provide **Visual Evidence** and **Immediate Proactive Signaling**.

This document outlines the architecture for integrating sparse video decoding for thumbnails and a webhook-based alerting system.

---

## 2. Visual Forensics: Sparse Thumbnail Generation

While TsAnalyzer's core engine remains zero-copy and non-decoding to maintain 1Gbps performance, providing a visual "peek" into the stream is essential for remote operations.

### 2.1 Principle of "Sparse Snapshotting"
Instead of a continuous decode, TsAnalyzer will implement a high-latency, low-priority worker thread:
*   **Trigger**: The `NALU Sniffer` identifies a Video IDR-Frame (I-Frame).
*   **Action**: If a thumbnail is requested (or at a fixed interval like every 10 seconds), the engine clones the current IDR PES packet into a side-buffer.
*   **Processing**: A background worker utilizing `libavcodec` (FFmpeg) performs a single-frame decode and scales it to a standard resolution (e.g., 320x240).
*   **Output**: The frame is encoded as a low-quality JPEG and stored in a shared memory slot or served via a dedicated REST endpoint `/api/v1/thumbnail/:stream_id`.

### 2.2 Performance Safeguards
*   **Isolation**: Decoding happens in a separate thread with lower scheduling priority (`SCHED_BATCH` or nice value) to ensure it never interferes with the real-time metrology loops.
*   **Sparse Sampling**: Limits decoding to at most 1 frame per $N$ seconds to prevent CPU exhaustion on high-motion 4K streams.

---

## 3. Active Alerting: Webhook Signaling Engine

Relying solely on Prometheus "pull" metrics can introduce up to 15-30 seconds of latency in alarm detection. Mission-critical events require sub-second notification.

### 3.1 Proactive JSON Webhooks
The `tsa_event.c` dispatcher will be extended with an **Active Signaling Plugin**:
*   **Mechanism**: When a critical event is pushed via `tsa_push_event()` (e.g., `TSA_EVENT_SYNC_LOSS` or `TSA_EVENT_SCTE35`), the engine immediately queues a Webhook task.
*   **Payload**: A compact JSON object containing:
    *   `stream_id`: Unique identifier.
    *   `event_type`: The TR 101 290 or SCTE-35 trigger.
    *   `timestamp_ns`: Precise hardware timestamp of the occurrence.
    *   `details`: Contextual metadata (e.g., SCTE-35 splice time, or CC gap size).
*   **Delivery**: Uses a non-blocking HTTP client (e.g., via Mongoose or a lightweight `libcurl` wrapper) to POST the payload to a user-configured management URL.

### 3.2 Event Throttling (Anti-Storm)
To prevent a network outage from triggering thousands of redundant HTTP requests:
*   **Dampening**: Alarms of the same type on the same stream are suppressed for $X$ seconds after the initial notification.
*   **Stateful Notification**: Supports `Raised` and `Cleared` messages to provide a coherent incident lifecycle.

---

## 4. Integration Roadmap (Future Tasks)

1.  **Metrology Core Completion**: Finalize all Layer 1-3 math before introducing external library dependencies like `libavcodec`.
2.  **Modular Dependency**: Ensure `FFmpeg` support is an optional build-time flag (`-DENABLE_VISUAL_FORENSICS=ON`).
3.  **Active Signaling implementation**: Add `webhook_url` to the `tsa_config_t` and implement the async POST worker.
4.  **UI Integration**: Update the Server Pro dashboard to display the latest thumbnail alongside the metrology graphs.
