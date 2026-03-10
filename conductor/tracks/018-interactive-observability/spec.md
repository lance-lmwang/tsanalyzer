# Specification: Interactive Observability (WebRTC & OTLP)

## 1. Objective
Transform TsAnalyzer into an interactive operations tool by adding low-latency visual feedback (WebRTC Preview) and modern observability integration (OpenTelemetry Tracing).

## 2. Requirements
- **WebRTC NOC Preview**: Allow NOC operators to "one-click" open a low-latency live preview (WHIP/WHEP) of any monitored stream in the browser.
- **OTLP Trace Support**: Export alert events and state transitions (e.g., "Stream Started" -> "Sync Loss" -> "Recovered") as OTLP Traces/Spans.
- **Context-Aware Alerting**: Link Prometheus metrics and OTLP traces with a single `trace_id` for integrated troubleshooting.
- **Interactive TUI (tsa_top)**: Enhance the existing TUI to support real-time stream status and quick actions (e.g., restart, config edit).

## 3. Architecture: The WebRTC Proxy
- **Integrated WHIP**: Implement a lightweight WHIP/WHEP gateway (or integrate with an external proxy like Janus/Mediamtx).
- **Just-in-Time Transcoding**: Only transcode the stream to WebRTC-compatible formats (VP8/H.264 + Opus) when an active viewer is connected.
- **OTLP Exporter**: Use the `opentelemetry-cpp` library or a direct OTLP-over-gRPC implementation for high-density trace export.

## 4. Integration KPIs
- `preview_session_count`
- `otlp_trace_dispatch_count`
- `ui_latency_ms`
- `otlp_export_status`
