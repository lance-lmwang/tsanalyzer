#!/bin/bash
# T-STD 33-bit DTS Wrap-around Regression Test
# Purpose: Verify that the engine handles the 33-bit rollover (every 26.5h) without false jump detection.
# Method: Use -copyts with a setpts-shifted stream to hit the 33-bit boundary.

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
OUT_DIR="${ROOT_DIR}/output"
mkdir -p "$OUT_DIR"

# Exact path logic from smoke test
FFMPEG_ROOT="$(cd "$ROOT_DIR/../ffmpeg.wz.master" && pwd)"
ffm="${FFMPEG_ROOT}/ffdeps_img/ffmpeg/bin/ffmpeg"
# Fallback to local built ffmpeg if ffdeps is not the one (some envs use local)
if [ ! -f "$ffm" ]; then
    ffm="${FFMPEG_ROOT}/ffmpeg"
fi

dst="${OUT_DIR}/tstd_wrap_test.ts"
log_file="${OUT_DIR}/tstd_wrap_test.log"

echo "[*] Starting T-STD 33-bit Wrap Test (using -copyts)..."
echo "[*] Using ffmpeg: $ffm"

# 2^33 is 8589934592.
# 8589930000 / 90000 = 95443.66 seconds.
$ffm -y -v trace -copyts -f lavfi -i "testsrc=duration=5:size=320x240:rate=25" \
      -vf "setpts='mod(N*3600 + 8589930000, 8589934592)'" \
      -c:v libx264 -b:v 200k -preset ultrafast \
      -f mpegts -muxrate 400k \
      -mpegts_tstd_mode 1 \
      "$dst" > "$log_file" 2>&1

# Rollover happens when DTS goes from ~8.58B back to 0.
# My fix ensures this is NOT detected as a jump.
ROLLOVER_WARNING=$(grep "Time jump" "$log_file" | grep -v "Baseline")

if [ -n "$ROLLOVER_WARNING" ]; then
    echo -e "\033[31m[FAIL] 33-bit rollover triggered a false Time Jump detection!\033[0m"
    echo "Warning found: $ROLLOVER_WARNING"
    exit 1
else
    echo -e "\033[32m[PASS] 33-bit rollover handled smoothly without false jump detection.\033[0m"
    exit 0
fi
