# Track: HLS/HTTP Ingest Engine

## 1. Goal
Enable `tsanalyzer` to consume Transport Streams from HLS (HTTP Live Streaming) URLs.

## 2. Technical Strategy
- **HTTP Client**: Use `libcurl` for segment downloading.
- **M3U8 Parser**: Implement a lightweight C parser (ported from libeasyice).
- **Architecture**:
    - `tsa_hls_ingest`: Background thread that polls M3U8 and manages download queue.
    - `tsa_hls_buffer`: Double-buffer or ring-buffer to store downloaded TS segments.
    - Integration into `tsa_source.c`.

## 3. Implementation Steps
- [x] Integration of `libcurl` and `zlib` into CMake.
- [x] Create `src/tsa_hls_parser.c` and `include/tsa_hls_parser.h`.
- [x] Implement M3U8 polling logic (detecting new segments).
- [x] Implement asynchronous segment downloader.
- [x] Verification with live HLS streams.
