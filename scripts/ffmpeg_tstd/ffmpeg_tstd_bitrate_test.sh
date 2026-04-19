#!/bin/bash
# ==============================================================================
# T-STD Bitrate Precision Test (Independent Module)
# Purpose: Verify CBR smoothness (1.0s and 0.5s windows)
# ==============================================================================

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
FFMPEG_ROOT="$(cd "$ROOT_DIR/../ffmpeg.wz.master" && pwd)"
FFMPEG="${FFMPEG_ROOT}/ffmpeg"
if [ ! -f "$FFMPEG" ]; then
    FFMPEG="${FFMPEG_ROOT}/ffdeps_img/ffmpeg/bin/ffmpeg"
fi

# Test settings
SRC="/home/lmwang/dev/cae/sample/af2_srt_src.ts"
DURATION=30
BITRATE="1600k"
MUXRATE=2000000
OUT_DIR="${ROOT_DIR}/output"
OUT_TS="${OUT_DIR}/bitrate_test.ts"
OUT_LOG="${OUT_DIR}/bitrate_test.log"

mkdir -p "$OUT_DIR"

echo "[*] Running High-Precision Bitrate Test (30s)..."
$FFMPEG -y -hide_banner -v trace -i "$SRC" \
      -t $DURATION \
      -map 0:v:0 -map 0:a:0 \
      -c:a libfdk_aac -b:a 128k \
      -vcodec libwz264 \
      -wz264-params bframes=0:keyint=25:vbv-maxrate=$BITRATE:vbv-bufsize=$BITRATE:nal-hrd=cbr:force-cfr=1:aud=1 \
      -b:v $BITRATE -preset medium \
      -f mpegts -muxrate $MUXRATE -muxdelay 0.9 -mpegts_tstd_mode 1 \
      "$OUT_TS" > "$OUT_LOG" 2>&1

if [ $? -eq 0 ]; then
    echo "[SUCCESS] FFmpeg run finished. Verifying metrics..."
    ${ROOT_DIR}/scripts/ffmpeg_tstd/tstd_telemetry_analyzer.py "$OUT_LOG"
    exit $?
else
    echo "[ERROR] FFmpeg crashed during bitrate test."
    exit 1
fi
