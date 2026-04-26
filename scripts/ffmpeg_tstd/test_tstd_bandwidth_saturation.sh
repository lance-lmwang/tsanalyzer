#!/bin/bash
# T-STD Negative Test: Bandwidth Saturation Detection
# Purpose: Verify that the engine correctly detects and logs when Muxrate is physically insufficient.

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
FFMPEG_ROOT="$(cd "$SCRIPT_DIR/../../../ffmpeg.wz.master" && pwd)"
ffm="${FFMPEG_ROOT}/ffdeps_img/ffmpeg/bin/ffmpeg"
OUT_DIR="$(cd "$SCRIPT_DIR/../../" && pwd)/output"
mkdir -p "$OUT_DIR"

SAT_LOG="${OUT_DIR}/tstd_saturation_test.log"
src="/home/lmwang/dev/cae/sample/input.mp4"

echo "========================================================="
echo " RUNNING T-STD NEGATIVE TEST: Bandwidth Saturation"
echo " Target: Vid 3.0M vs Mux 2.5M (Deliberate Starvation)"
echo "========================================================="

if [ ! -f "$src" ]; then
    echo "[ERROR] Source sample not found: $src"
    exit 1
fi

# Run for 30s to ensure delay builds up past the 1500ms threshold
$ffm -y -hide_banner -i "$src" -t 30 \
      -c:v libwz264 -b:v 3000k -preset ultrafast \
      -f mpegts -muxrate 2500k -mpegts_tstd_mode 1 -tstd_params "debug=2" \
      "${OUT_DIR}/saturation_test.ts" > "$SAT_LOG" 2>&1

echo "[*] Analyzing log: $SAT_LOG"

# Check for the specific expert warning added in libavformat/tstd.c
if grep -q "PHYSICAL SATURATION" "$SAT_LOG"; then
    echo -e "\033[32m[PASS] Physical Saturation Guard triggered correctly.\033[0m"
    grep "PHYSICAL SATURATION" "$SAT_LOG" | tail -n 1 | sed 's/^/    - /'
    exit 0
else
    echo -e "\033[31m[FAIL] Physical Saturation NOT detected! Logic failure.\033[0m"
    # Show last few T-STD logs for context
    grep "\[T-STD SEC\]" "$SAT_LOG" | tail -n 5
    exit 1
fi
