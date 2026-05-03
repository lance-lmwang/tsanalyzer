#!/bin/bash
# Truth Check: Compare Log-based Audit vs. Physical Bitstream Audit
# To verify consistency between FFmpeg internal telemetry and the actual TS on disk.

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/../../.." && pwd)"
OUT_DIR="${ROOT_DIR}/output/truth_check"
mkdir -p "$OUT_DIR"

FFMPEG_BIN="${ROOT_DIR}/../ffmpeg.wz.master/ffdeps_img/ffmpeg/bin/ffmpeg"
AUDITOR_PY="${SCRIPT_DIR}/ts_expert_auditor.py"

VBR="600"
MUX="1100k"
SRC="${ROOT_DIR}/../sample/knet_sd_03.ts"
TS_OUT="${OUT_DIR}/truth.ts"
LOG_OUT="${OUT_DIR}/truth.log"

echo "[*] Running 600k/1100k Stress Test..."
export WZ_LICENSE_KEY="${ROOT_DIR}/../wz_license.key"
$FFMPEG_BIN -y -hide_banner -i "$SRC" -t 30 \
      -c:v libwz264 -b:v "${VBR}k" -preset fast -wz264-params "keyint=25:vbv-maxrate=$VBR:vbv-bufsize=$VBR:nal-hrd=cbr:force-cfr=1:aud=1:scenecut=0:b-adapt=0" \
      -c:a aac -b:a 128k \
      -f mpegts -muxrate $MUX -mpegts_start_pid 0x21 -mpegts_pcr_pid 0x21 -mpegts_tstd_mode 1 -mpegts_tstd_debug 2 \
      "$TS_OUT" > "$LOG_OUT" 2>&1

echo ""
echo "===================================================="
echo "   METHOD A: Internal Engine Telemetry (Mean/Max)"
echo "===================================================="
grep "\[T-STD SEC\]" "$LOG_OUT" | awk -F'Out:' '{print $2}' | awk '{print $1}' | sed 's/k//g' | awk '{sum+=$1; count++; if($1>max) max=$1} END {if(count>0) printf "Mean Bitrate: %.2f kbps\nMax Bitrate:  %.2f kbps\n", sum/count, max}'

echo ""
echo "===================================================="
echo "   METHOD B: Physical Bitstream Analyzer (The Truth)"
echo "===================================================="
if [ -f "$AUDITOR_PY" ]; then
    python3 "$AUDITOR_PY" "$TS_OUT" --vid 0x21 --target $VBR --skip 5.0
else
    echo "[ERROR] Physical auditor not found at $AUDITOR_PY"
fi
