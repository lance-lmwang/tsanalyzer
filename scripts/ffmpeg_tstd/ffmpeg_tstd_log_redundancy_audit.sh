#!/bin/bash
# Analysis tool for T-STD Log Redundancy and Multiplexer Spinning
# Used to determine why 'Drive:' and other logs repeat unnecessarily.

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
OUT_DIR="${ROOT_DIR}/output/log_analysis"
mkdir -p "$OUT_DIR"

FFMPEG_BIN="${ROOT_DIR}/../ffmpeg.wz.master/ffdeps_img/ffmpeg/bin/ffmpeg"
VBR="600k"
MUX="1200k"
SRC="/home/lmwang/dev/cae/sample/af2_srt_src.ts"
LOG_OUT="${OUT_DIR}/spinning.log"

echo "[*] Capturing 600k/1200k Start-up phase (10s)..."
$FFMPEG_BIN -y -hide_banner -v trace -i "$SRC" -t 10 \
      -c:v libwz264 -b:v $VBR -preset ultrafast -wz264-params bframes=0:keyint=25:vbv-maxrate=$VBR:vbv-bufsize=$VBR:nal-hrd=cbr:force-cfr=1:aud=1 \
      -c:a aac -b:a 128k \
      -f mpegts -muxrate $MUX -mpegts_tstd_mode 1 \
      "${OUT_DIR}/spinning.ts" > "$LOG_OUT" 2>&1

echo ""
echo "================================================================"
echo "   T-STD LOG REDUNDANCY ANALYSIS (First 5 seconds)"
echo "================================================================"

# 1. Check for consecutive identical logs
echo "[*] Consecutive Duplicate 'Drive' Logs (Spinning Detection):"
grep "\[T-STD\] Drive: " "$LOG_OUT" | uniq -c | head -n 20

# 2. Check the ratio of Drive vs PES/NULL actions
TOTAL_DRIVE=$(grep -c "\[T-STD\] Drive: " "$LOG_OUT")
TOTAL_PES=$(grep -c "ACT:PES" "$LOG_OUT")
TOTAL_NULL=$(grep -c "ACT:NULL" "$LOG_OUT")

echo ""
echo "[*] Log Frequency Statistics:"
echo "    - Total 'Drive:' lines:  $TOTAL_DRIVE"
echo "    - Total 'PES:' packets:  $TOTAL_PES"
echo "    - Total 'NULL:' packets: $TOTAL_NULL"

RATIO=$(echo "scale=2; $TOTAL_DRIVE / ($TOTAL_PES + $TOTAL_NULL + 1)" | bc)
echo "    - Spinning Ratio (Drive / Packet): $RATIO"

if (( $(echo "$RATIO > 1.0" | bc -l) )); then
    echo -e "\n[RESULT] \033[31mHIGH SPINNING DETECTED.\033[0m"
    echo "Multiplexer is spinning its clock without outputting packets."
else
    echo -e "\n[RESULT] \033[32mLOG DENSITY IS NORMAL.\033[0m"
fi
echo "================================================================"
