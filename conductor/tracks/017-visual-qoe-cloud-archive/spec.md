# Specification: Visual Quality & Cloud Archive (QoE & S3)

## 1. Objective
Enhance the visual verification capabilities of TsAnalyzer by implementing IDR thumbnailing, black/freeze screen detection, and event-triggered forensic recording for automated cloud archiving.

## 2. Requirements
- **IDR Thumbnailer**: Periodically extract and decode one IDR frame (e.g., every 5-30s) to generate a Base64 or JPEG thumbnail.
- **Visual QoE Analysis**: Detect visual impairments: Black Screen, Static Frame (Freeze), and Macro-blocking.
- **Event-Triggered S3 Archive**: Automatically capture a short `.ts` segment (e.g., 10s pre-roll + 10s post-roll) on critical errors (Sync Loss, TR P1) and upload to S3/OSS.
- **Forensic Time Machine**: Maintain a small 2-minute rolling ring-buffer in memory for instant replay on alert.

## 3. Architecture: The Audit Worker (FFmpeg-Lite)
- **Sidecar Decoding**: Use a lightweight `FFmpeg/libavcodec` instance (with HW acceleration support) for IDR-only decoding to minimize CPU impact.
- **Entropy Analysis**: Use Shannon entropy on the Luma plane to differentiate between a black screen and a static image (frozen frame).
- **Asynchronous Uploader**: Use a non-blocking background thread to push `.ts` segments to S3/OSS, ensuring that slow WAN uploads do not contaminate the analysis pipeline.

## 4. Operational KPIs
- `qoe_black_detect_bool`
- `qoe_freeze_detect_bool`
- `qoe_thumbnail_uri`
- `archive_s3_upload_status`
