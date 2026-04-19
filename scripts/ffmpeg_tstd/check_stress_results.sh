#!/bin/bash
# T-STD Stress Test Post-Mortem Analyzer

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
OUT_DIR="${ROOT_DIR}/output/overnight_stress"
LOG_FILE="${OUT_DIR}/ffmpeg.log"
AUDITOR_PY="${SCRIPT_DIR}/ts_expert_auditor.py"

echo "=========================================================="
echo "   T-STD 8-HOUR STRESS TEST ANALYSIS REPORT"
echo "=========================================================="

if [ ! -f "$LOG_FILE" ]; then
    echo "[ERROR] Stress log not found at $LOG_FILE"
    exit 1
fi

echo "[1/4] Scanning for Clock & Pacing Failures..."
ERROR_COUNT=$(grep -E "DRIVE FUSE|PANIC|STC JUMP|LATE CLAMP" "$LOG_FILE" | wc -l)
if [ "$ERROR_COUNT" -eq 0 ]; then
    echo -e "    \033[32m[PASS] Zero clock domain or pacing failures detected.\033[0m"
else
    echo -e "    \033[31m[FAIL] Detected $ERROR_COUNT stability warnings! Review log.\033[0m"
    grep -E "DRIVE FUSE|PANIC|STC JUMP|LATE CLAMP" "$LOG_FILE" | head -n 10
fi

echo "[2/4] Analyzing VBV Headroom Trends..."
MAX_VBV=$(grep "\[T-STD SEC\]" "$LOG_FILE" | awk -F'VBV:' '{print $2}' | awk -F'%' '{print $1}' | sort -rn | head -n 1)
AVG_VBV=$(grep "\[T-STD SEC\]" "$LOG_FILE" | awk -F'VBV:' '{print $2}' | awk -F'%' '{print $1}' | awk '{sum+=$1} END {print sum/NR}')
echo "    - Max VBV Occupancy: ${MAX_VBV}%"
echo "    - Avg VBV Occupancy: ${AVG_VBV}%"

echo "[3/4] Bitrate Consistency Check (Final 10 Minutes)..."
# Pull the last 600 samples
grep "\[T-STD SEC\]" "$LOG_FILE" | tail -n 600 | awk -F'Out:' '{print $2}' | awk '{print $1}' | sed 's/k//g' | awk '{sum+=$1; count++; if($1>max) max=$1; if($1<min || min=="") min=$1} END {printf "    - Avg Rate: %.2f kbps\n    - Max Rate: %.2f kbps (+%.2f%%)\n    - Min Rate: %.2f kbps\n", sum/count, max, (max/1600-1)*100, min}'

echo "[4/4] Stream Integrity Verification..."
$ROOT_DIR/../ffmpeg.wz.master/ffdeps_img/ffmpeg/bin/ffprobe -i "${OUT_DIR}/stress_sample.ts" -hide_banner 2>&1 | grep -iE "error|corrupt"
if [ $? -eq 0 ]; then
    echo -e "    \033[31m[FAIL] Potential stream corruption detected.\033[0m"
else
    echo -e "    \033[32m[PASS] Bitstream remains compliant.\033[0m"
fi

echo "=========================================================="
echo "Report Generated at: $(date)"
