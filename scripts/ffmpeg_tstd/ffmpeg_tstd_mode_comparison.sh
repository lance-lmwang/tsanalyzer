#!/bin/bash
# T-STD Mode Comparison Test Script
# Compares Mode 1 (Weight=4) vs Mode 2 (Weight=16)

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
FFMPEG_ROOT="$(cd "$ROOT_DIR/../ffmpeg.wz.master" && pwd)"
ffm="${FFMPEG_ROOT}/ffdeps_img/ffmpeg/bin/ffmpeg"
src="${ROOT_DIR}/../sample/af2_srt_src.ts"
OUT_DIR="${ROOT_DIR}/output/comparison"
mkdir -p "$OUT_DIR"

run_test() {
    local mode=$1
    local name=$2
    local log_file="${OUT_DIR}/mode_${mode}.log"
    local dst="${OUT_DIR}/mode_${mode}.ts"

    echo "[*] Running Test: $name (Mode $mode)..."

    # Run for 20s to get steady state
    cmd="$ffm -y -v trace -i '$src' -t 20 \
          -map 0:v:0 -map 0:a:0 \
          -c:a libfdk_aac -b:a 128k -vcodec libwz264 \
          -wz264-params bframes=0:keyint=25:vbv-maxrate=1600:vbv-bufsize=1600:nal-hrd=cbr -threads 4 \
          -b:v 1600k -f mpegts -muxrate 2000000 -mpegts_tstd_mode $mode \
          '$dst' > $log_file 2>&1"
    eval $cmd

    echo "[*] Analyzing Mode $mode..."
    ${ROOT_DIR}/scripts/ffmpeg_tstd/tstd_bitrate_auditor.py --log "$log_file" --window 1.0 --skip 3.0 > "${OUT_DIR}/report_mode_${mode}.txt"
}

run_test 1 "Strict (Weight 4)"
run_test 2 "Elastic (Weight 16)"

echo ""
echo "===================================================="
echo "          T-STD MODE COMPARISON SUMMARY"
echo "===================================================="
printf "%-20s | %-15s | %-15s\n" "Metric (1s window)" "Mode 1 (Strict)" "Mode 2 (Elastic)"
echo "----------------------------------------------------"

fluct1=$(grep "Fluctuation:" "${OUT_DIR}/report_mode_1.txt" | awk '{print $2}')
fluct2=$(grep "Fluctuation:" "${OUT_DIR}/report_mode_2.txt" | awk '{print $2}')
printf "%-20s | %-15s | %-15s\n" "Fluctuation (kbps)" "$fluct1" "$fluct2"

mean1=$(grep "Mean Bitrate:" "${OUT_DIR}/report_mode_1.txt" | awk '{print $3}')
mean2=$(grep "Mean Bitrate:" "${OUT_DIR}/report_mode_2.txt" | awk '{print $3}')
printf "%-20s | %-15s | %-15s\n" "Mean Bitrate (kbps)" "$mean1" "$mean2"

std1=$(grep "Standard Dev:" "${OUT_DIR}/report_mode_1.txt" | awk '{print $3}')
std2=$(grep "Standard Dev:" "${OUT_DIR}/report_mode_2.txt" | awk '{print $3}')
printf "%-20s | %-15s | %-15s\n" "StdDev (kbps)" "$std1" "$std2"
echo "===================================================="
