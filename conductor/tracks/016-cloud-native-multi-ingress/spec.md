# Specification: Cloud-Native Multi-Protocol Ingress (DASH & LL-HLS)

## 1. Objective
Expand the input capabilities beyond traditional TS/SRT to support modern OTT delivery protocols (MPEG-DASH, LL-HLS) for full end-to-end cloud distribution monitoring.

## 2. Requirements
- **MPEG-DASH Support**: Parse MPD (Media Presentation Description) manifests, monitor representation drift, and fetch segments.
- **LL-HLS (Low Latency)**: Audit HLS `Part` segments and `Preload Hints` to measure real-world latency vs. target latency.
- **ABR Variant Alignment (Skew)**: Track temporal alignment (skew) across multiple renditions (variants) to ensure smooth switching.
- **CDN Latency Monitoring**: Track TTFB (Time To First Byte) and download throughput for every segment fetch.

## 3. Architecture: The Pull Dispatcher
- **Async Poller**: Utilize a non-blocking `http_client` (e.g., based on Mongoose or `libcurl`) to poll manifests and fetch segments.
- **Virtual Stream Model**: Treat an OTT stream as a sequence of fetched TS/CMAF segments. Each segment is pushed through the existing L1-L4 metrology reactor once downloaded.
- **Clock Alignment**: Map the Manifest-time (Wall clock) to the Segment-internal PCR/Media clock to detect drift between the CDN delivery and the media timeline.

## 4. Metrology KPIs
- `ott_manifest_refresh_latency_ms`
- `ott_segment_download_speed_bps`
- `ott_representation_drift_seconds`
- `ott_hls_ll_prehint_miss_count`
