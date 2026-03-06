# HLS & OTT Delivery Analysis Model

To achieve industrial parity with tools like `libEasyIce`, TsAnalyzer extends its metrology beyond the Transport Stream into the HTTP Adaptive Streaming (HAS) layer.

---

## 1. HLS Protocol Auditing

The engine performs a multi-stage audit of the HLS session to ensure delivery continuity and manifest compliance.

### 1.1 Manifest Integrity (m3u8)
*   **Update Latency**: Monitoring the time delta between expected and actual playlist updates.
*   **Media Sequence Audit**: Detecting gaps or non-monotonic increments in `#EXT-X-MEDIA-SEQUENCE`.
*   **Discontinuity Tracking**: Auditing the validity of `#EXT-X-DISCONTINUITY` tags relative to the underlying video GOP structure.

### 1.2 Fragment Download Metrology
*   **Download Time (T_dl)**: The time taken to retrieve a media segment.
*   **Download Ratio**: $Ratio = \frac{Fragment\_Duration}{Download\_Time}$.
    *   *Constraint*: If $Ratio < 1.2$, the stream is at high risk of buffer starvation.
*   **TTFB (Time to First Byte)**: Measuring the network responsiveness of the CDN/Origin.

---

## 2. Manifest-Stream Correlation

A unique capability inspired by professional OTT probes is the correlation between the manifest intent and the TS reality.

| Metric | Check | Description |
| :--- | :--- | :--- |
| **Duration Accuracy** | Manifest vs TS | Comparing `#EXTINF` duration with the actual sum of PCR deltas. |
| **PTS Continuity** | Cross-Segment | Ensuring the last PTS of fragment $N$ aligns with the first PTS of fragment $N+1$. |
| **Profile Alignment** | Multi-Variant | Verifying that IDR frames across different bitrate variants share the same absolute VSTC timestamp. |

---

## 3. Operational Logic

1.  **HLS Ingest Worker**: An asynchronous fetcher that downloads fragments and pushes them into the standard `Ingestion Engine` (Layer 1).
2.  **Metadata Sidecar**: The manifest audit results are attached to the `tsa_handle_t` and exported via the JSON API as `ott_metadata`.
3.  **Alarming**: Triggers alerts for **Playlist Freeze**, **Fragment Fetch Timeout**, and **Cross-Variant Desync**.
