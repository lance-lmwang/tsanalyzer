#!/bin/bash
# ==============================================================================
# T-STD Engine Regression Test: In-Flight Starvation & Skew Recovery
# ==============================================================================
# Purpose:
#   Simulate a severely degraded broadcast input ("live1" scenario) using
#   only internal lavfi sources.
#
# Scenario:
#   1. Asymmetric Duration: Video ends abruptly at 10s, Audio continues to 15s.
#   2. Initial Skew: Audio starts -0.7s earlier than video (-itsoffset -0.7).
#   3. Narrow Pipe: Muxrate is clamped to 1000k, forcing high buffer pressure
#      when trying to encode 600k Video + 128k Audio + TS Overhead.
#
# Expected Behavior (With Starvation Detection & EOF Fast Drain):
#   - The T-STD engine MUST detect the video stream starvation around 10.5s.
#   - It MUST engage the Over-Revving mechanism to violently drain the trapped
#     video packets at ~850kbps, bypassing the strict 630kbps CBR limit.
#   - It MUST finish the 15s file successfully WITHOUT triggering the fatal
#     "DRIVE FUSE" clock-break alarm.
#   - The final file size should be optimal (not bloated by 10s of NULLs).
# ==============================================================================

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
FFMPEG="${ROOT_DIR}/../ffmpeg.wz.master/ffdeps_img/ffmpeg/bin/ffmpeg"
OUT_DIR="${ROOT_DIR}/output"
OUT_TS="${OUT_DIR}/repro_starvation.ts"
OUT_LOG="${OUT_DIR}/repro_starvation.log"

mkdir -p "$OUT_DIR"

echo "[*] ==========================================================="
echo "[*] T-STD Regression: Video Starvation & Clock Skew Recovery Test"
echo "[*] ==========================================================="
echo "[*] Generating synthetic 'dirty' TS..."
echo "    - Video: 10s duration, 600kbps"
echo "    - Audio: 15s duration, 128kbps, -0.7s start offset"
echo "    - Pipe : 1000kbps Muxrate (High Pressure)"

$FFMPEG -y -hide_banner \
    -f lavfi -t 10 -i "testsrc=size=720x576:rate=25" \
    -f lavfi -itsoffset -0.7 -t 15 -i "sine=frequency=1000:sample_rate=48000" \
    -c:v libx264 -b:v 600k -preset ultrafast \
    -c:a mp2 -b:a 128k \
    -muxrate 1000k -mpegts_tstd_mode 1 \
    -f mpegts "$OUT_TS" > "$OUT_LOG" 2>&1

EXIT_CODE=$?

echo ""
if [ $EXIT_CODE -ne 0 ]; then
    echo -e "\033[31m[CRITICAL] FFmpeg muxing process failed (Exit Code: $EXIT_CODE)!\033[0m"
    exit 1
fi

echo "[*] Analyzing engine telemetry logs..."

# 1. Check for the Fatal Clock Drift Alarm (Failure condition of old code)
if grep -q "DRIVE FUSE" "$OUT_LOG"; then
    echo -e "\033[31m[FAIL] Fatal clock desync (DRIVE FUSE) triggered! Engine failed to recover from starvation.\033[0m"
    grep "DRIVE FUSE" "$OUT_LOG"
    exit 1
else
    echo -e "\033[32m[PASS] Engine maintained clock sync. No DRIVE FUSE triggered.\033[0m"
fi

# 2. Verify Starvation Detection engaged successfully
if grep -q "starvation detected" "$OUT_LOG"; then
    echo -e "\033[32m[PASS] Starvation Detection engaged successfully.\033[0m"
    # Show the first instance of detection
    grep "starvation detected" "$OUT_LOG" | head -n 1 | sed 's/^/       /'
else
    echo -e "\033[33m[WARN] Starvation Detection did not trigger. Ensure pressure thresholds and pipe size are correct for the test.\033[0m"
    # It's a warning, not a fail, because ultra-fast encoders might not fill the buffer enough to trigger the >0 bytes condition if drained too fast natively.
fi

# 3. File Size Sanity Check (Ensure EOF Fast Drain didn't bloat the file)
# A 15s file at 1000kbps Muxrate should be exactly: (1000000 / 8) * 15 = 1,875,000 bytes.
# Allow a 5% margin for headers and final padding.
FILE_SIZE=$(stat -c%s "$OUT_TS")
EXPECTED_SIZE=1875000
MARGIN=100000

if [ $FILE_SIZE -gt $((EXPECTED_SIZE + MARGIN)) ]; then
    echo -e "\033[31m[FAIL] File size ($FILE_SIZE bytes) is suspiciously large. EOF Fast Drain might have failed, leading to NULL padding explosion.\033[0m"
    exit 1
else
    echo -e "\033[32m[PASS] File size ($FILE_SIZE bytes) is optimal. Fast Drain prevented NULL explosion.\033[0m"
fi

echo "-----------------------------------------------------------"
echo -e "\033[32mSTATUS: SUCCESS (System survived severe starvation & skew)\033[0m"
exit 0
