#!/bin/bash
# 30s Quick Audit Script
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
FFMPEG_ROOT="$(cd "$ROOT_DIR/../ffmpeg.wz.master" && pwd)"
AUDIT_TOOL="${SCRIPT_DIR}/tstd_audio_sanity_audit.sh"

NAME="1080i_30s"
FILE="hd-2026.3.13-10.20~10.25.ts"
OUT_TS="${ROOT_DIR}/output_golden/tstd_${NAME}_v2_aligned.ts"
LOG_FILE="${ROOT_DIR}/output_golden/tstd_${NAME}_v2_aligned.log"

echo "[*] Running 30s Stress Test for $NAME..."

# Use direct ffmpeg to control duration precisely
"${FFMPEG_ROOT}/ffdeps_img/ffmpeg/bin/ffmpeg" -y -hide_banner \
    -i "${ROOT_DIR}/../sample/$FILE" -t 30 \
    -c:v libwz264 -b:v 1500k -g 25 -c:a mp2 -b:a 128k \
    -f mpegts -muxrate 2300k -muxdelay 0.9 \
    -mpegts_tstd_mode 1 -mpegts_tstd_debug 1 \
    "$OUT_TS" > "$LOG_FILE" 2>&1

echo "[*] Triggering Quantified Audit..."
"$AUDIT_TOOL" "$OUT_TS"
