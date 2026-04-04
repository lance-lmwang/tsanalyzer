#!/bin/bash
# Test FFmpeg with -copyts on a file with PCR rollbacks/jumps
# 1. Normal Mode (T-STD Off)
# 2. Strict Mode (T-STD On)

ROOT_DIR=$(dirname $(dirname $(dirname $(readlink -f $0))))
FFMPEG_BIN="${ROOT_DIR}/../ffmpeg.wz.master/ffdeps_img/ffmpeg/bin/ffmpeg"
SRC="/home/lmwang/sample/jaco/202508300200_Al-Taawoun_VS_Al-Nassr_2_cut_100M.ts"
MUXRATE=1000000

# We use -t 60 to limit the test duration to 60s of stream time
echo "=== Testing Jaco Sample with PCR Jumps (Limited to 60s) ==="

# --- Test 1: Normal -copyts ---
echo "[Test 1] Running FFmpeg with -copyts, T-STD mode OFF..."
$FFMPEG_BIN -y -v debug -i "$SRC" -copyts -c copy -f mpegts -muxrate $MUXRATE -t 60 out_normal.ts > normal.log 2>&1
if [ $? -eq 0 ]; then
    echo "[SUCCESS] Normal mode finished."
    ${ROOT_DIR}/scripts/ffmpeg_tstd/ffmpeg_tstd_verify_compliance.sh normal.log
else
    echo "[ERROR] Normal mode failed. Check normal.log"
fi

echo "------------------------------------------"

# --- Test 2: T-STD -copyts ---
echo "[Test 2] Running FFmpeg with -copyts, T-STD mode ON..."
$FFMPEG_BIN -y -v debug -i "$SRC" -copyts -c copy -f mpegts -muxrate $MUXRATE -mpegts_tstd_mode 1 -t 60 out_tstd.ts > tstd.log 2>&1
if [ $? -eq 0 ]; then
    echo "[SUCCESS] T-STD mode finished."
    ${ROOT_DIR}/scripts/ffmpeg_tstd/ffmpeg_tstd_verify_compliance.sh tstd.log
else
    echo "[ERROR] T-STD mode failed. Check tstd.log"
fi
