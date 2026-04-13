#!/bin/bash
# Truth Check: Compare Log-based Audit vs. Physical Bitstream Audit
# To expose if the auditor script is double-counting log lines.

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
OUT_DIR="${ROOT_DIR}/output/truth_check"
mkdir -p "$OUT_DIR"

FFMPEG_BIN="${ROOT_DIR}/../ffmpeg.wz.master/ffdeps_img/ffmpeg/bin/ffmpeg"
LOG_AUDITOR="${SCRIPT_DIR}/tstd_bitrate_auditor.py"
# Production analyzer that reads the actual .ts file
PHYS_AUDITOR="${SCRIPT_DIR}/ts_pid_bitrate_pcr_analyzer.py"

VBR="600k"
MUX="1200k"
SRC="/home/lmwang/dev/cae/sample/af2_srt_src.ts"
TS_OUT="${OUT_DIR}/truth.ts"
LOG_OUT="${OUT_DIR}/truth.log"

echo "[*] Running 600k/1200k Stress Test..."
$FFMPEG_BIN -y -hide_banner -v trace -i "$SRC" -t 30 \
      -c:v libwz264 -b:v $VBR -preset ultrafast -wz264-params bframes=0:keyint=25:vbv-maxrate=$VBR:vbv-bufsize=$VBR:nal-hrd=cbr:force-cfr=1:aud=1 \
      -c:a aac -b:a 128k \
      -f mpegts -muxrate $MUX -mpegts_tstd_mode 1 \
      "$TS_OUT" > "$LOG_OUT" 2>&1

echo ""
echo "===================================================="
echo "   METHOD A: Log-based Auditor (Counting Lines)"
echo "===================================================="
python3 "$LOG_AUDITOR" --log "$LOG_OUT" --pid 0x0100 --window 1.0 --skip 5.0 --skip-tail 5.0

echo ""
echo "===================================================="
echo "   METHOD B: Physical Bitstream Analyzer (The Truth)"
echo "===================================================="
if [ -f "$PHYS_AUDITOR" ]; then
    # ts_pid_bitrate_pcr_analyzer.py usually outputs Mean and Fluctuation
    python3 "$PHYS_AUDITOR" "$TS_OUT" --pid 0x0100 --window 1.0 --skip 5.0 | grep -E "Mean Bitrate|Fluctuation"
else
    echo "[ERROR] Physical auditor not found at $PHYS_AUDITOR"
fi
