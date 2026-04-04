#!/bin/bash
# Bit-exact ES Layer Verification Suite
set -e

FFMPEG_BIN="../../ffmpeg.wz.master/ffdeps_img/ffmpeg/bin/ffmpeg"
FRAMES=500

echo "====================================================="
echo "  T-STD Bit-Exact Decodability Audit (500 Frames) "
echo "====================================================="

# 1. Clean up
rm -f raw.h264 ref.ts test.ts ref_md5.txt test_md5.txt

# 2. Generate a pure, deterministic Raw H.264 stream
echo "[*] Phase 1: Generating Raw H.264 ($FRAMES frames)..."
$FFMPEG_BIN -v error -f lavfi -i testsrc=duration=$(bc <<< "scale=2; $FRAMES/30"):size=1280x720:rate=30 \
    -c:v libx264 -preset ultrafast -tune zerolatency -b:v 2M -maxrate 2M -bufsize 2M \
    -x264-params "nal-hrd=cbr:force-cfr=1" \
    raw.h264

# 3. Mux Reference (Native FFmpeg, T-STD OFF)
echo "[*] Phase 2: Muxing Reference Stream (T-STD Mode 0)..."
$FFMPEG_BIN -v error -i raw.h264 -c copy -muxrate 3M -mpegts_tstd_mode 0 ref.ts

# 4. Mux Test (T-STD ON)
echo "[*] Phase 3: Muxing Test Stream (T-STD Mode 1)..."
$FFMPEG_BIN -v error -i raw.h264 -c copy -muxrate 3M -mpegts_tstd_mode 1 test.ts

# 5. Extract Frame MD5 Hashes
echo "[*] Phase 4: Decoding and Extracting Frame MD5s..."
$FFMPEG_BIN -v error -i ref.ts -f framemd5 ref_md5.txt
$FFMPEG_BIN -v error -i test.ts -f framemd5 test_md5.txt

# 6. Compare Hashes
echo "[*] Phase 5: Bit-Exact Verification..."
REF_FRAMES=$(grep "^0, " ref_md5.txt | wc -l)
TEST_FRAMES=$(grep "^0, " test_md5.txt | wc -l)

echo "Reference Frames Decoded: $REF_FRAMES"
echo "T-STD Stream Frames Decoded: $TEST_FRAMES"

if [ "$REF_FRAMES" -ne "$FRAMES" ]; then
    echo "[!] WARNING: Reference stream did not decode expected 500 frames."
fi

if [ "$REF_FRAMES" -ne "$TEST_FRAMES" ]; then
    echo "[FAIL] Frame count mismatch! T-STD Engine dropped or duplicated data."
    exit 1
fi

if diff -q ref_md5.txt test_md5.txt > /dev/null; then
    echo "[PASS] 100% BIT-EXACT MATCH! ES payload is perfectly preserved."
else
    echo "[FAIL] Checksums differ! T-STD Engine corrupted the payload."
    diff -u ref_md5.txt test_md5.txt | head -n 20
    exit 1
fi
