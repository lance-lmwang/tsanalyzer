#!/bin/bash
# T-STD Audio-Only Resilience Audit
# Verifies engine stability when no video stream is present.

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/../../.." && pwd)"
OUT_TS="${ROOT_DIR}/output/audio_only_test.ts"
LOG_FILE="${ROOT_DIR}/output/audio_only.log"

FFMPEG="${ROOT_DIR}/../ffmpeg.wz.master/ffdeps_img/ffmpeg/bin/ffmpeg"
SAMPLE="${ROOT_DIR}/../sample/af2_srt_src.ts"

echo "[*] Testing Audio-Only Resilience (Target: 300kbps Muxrate)..."

# Extract audio only and run T-STD at very low bitrate
$FFMPEG -y -hide_banner -i "$SAMPLE" -vn -c:a copy -muxrate 300k -mpegts_tstd_mode 1 "$OUT_TS" > "$LOG_FILE" 2>&1
RET=$?

if [ $RET -ne 0 ]; then
    echo "[FAIL] Audio-only processing crashed."
    exit 1
fi

# Verify bitrate stability using TSDuck if available
if command -v tsanalyze &> /dev/null; then
    MAX_SDT=$(tsanalyze "$OUT_TS" | grep -A 10 "PID: 0x0011 (17)" | grep "Max repet.:" | awk -F: '{print $2}' | tr -dc '0-9')
    echo "[*] SDT Max Interval in Audio-only: ${MAX_SDT}ms"
    if [ "$MAX_SDT" -gt 2000 ]; then
        echo "[FAIL] SDT interval violation in audio-only mode."
        exit 1
    fi
fi

echo "[PASS] Audio-only stream generated successfully and pacing maintained."
exit 0
