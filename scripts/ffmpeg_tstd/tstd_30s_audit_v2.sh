#!/bin/bash
# V2 Logic Realignment Audit (30s)
FFMPEG="/home/lmwang/dev/cae/ffmpeg.wz.master/ffdeps_img/ffmpeg/bin/ffmpeg"
INPUT="/home/lmwang/dev/cae/sample/hd-2026.3.13-10.20~10.25.ts"
OUT_DIR="/home/lmwang/dev/cae/tsanalyzer/output_golden"
OUT_TS="$OUT_DIR/tstd_1080i_v2_aligned.ts"
LOG_FILE="$OUT_DIR/tstd_1080i_v2_aligned.log"
AUDIT_TOOL="/home/lmwang/dev/cae/tsanalyzer/scripts/ffmpeg_tstd/tstd_audio_sanity_audit.sh"

echo "[*] Generating V2 Aligned Sample (30s) with 120s timeout..."
timeout 120s $FFMPEG -y -hide_banner -i "$INPUT" -t 30 \
    -c:v libwz264 -b:v 1500k -g 25 -c:a mp2 -b:a 128k \
    -f mpegts -muxrate 2300k -muxdelay 0.9 -mpegts_tstd_mode 1 \
    -mpegts_tstd_debug 1 \
    "$OUT_TS" > "$LOG_FILE" 2>&1

echo "[*] Running Physical Audit..."
$AUDIT_TOOL "$OUT_TS"
