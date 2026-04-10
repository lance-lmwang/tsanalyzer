#!/bin/bash
# T-STD Parameter Sweep Test Suite
# Purpose: Validate T-STD engine across wide range of bitrates and muxing settings.

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
OUT_DIR="${ROOT_DIR}/output/sweep"
mkdir -p "$OUT_DIR"

FFMPEG_ROOT="$(cd "$ROOT_DIR/../ffmpeg.wz.master" 2>/dev/null && pwd)"
if [ -z "$FFMPEG_ROOT" ] || [ ! -x "$FFMPEG_ROOT/ffdeps_img/ffmpeg/bin/ffmpeg" ]; then
    FFMPEG_BIN=$(which ffmpeg)
else
    FFMPEG_BIN="$FFMPEG_ROOT/ffdeps_img/ffmpeg/bin/ffmpeg"
fi

VERIFIER="$SCRIPT_DIR/ffmpeg_tstd_verify_compliance.sh"
SRC="/home/lmwang/sample/input.mp4"

# 1. Bitrate Sweep (500k to 50M)
BITRATES=("500000" "2000000" "10000000" "40000000")

echo "===================================================="
echo "   T-STD PARAMETER SWEEP TEST SUITE (v1.0)"
echo "===================================================="

for BR in "${BITRATES[@]}"; do
    # Calculate muxrate (approx 1.2x of VBR or 1.05x of CBR)
    # For T-STD we usually want bit-accurate CBR
    MUXRATE=$(echo "$BR * 1.2" | bc -l | awk -F'.' '{print $1}')

    echo ""
    echo "[SWEEP] Bitrate: $((BR/1000))kbps, Muxrate: $((MUXRATE/1000))kbps"

    FINAL_TS="$OUT_DIR/sweep_${BR}.ts"
    FINAL_LOG="$OUT_DIR/sweep_${BR}.log"

    $FFMPEG_BIN -y -v trace -i "$SRC" -t 5 \
        -c:v libx264 -b:v "$BR" -maxrate "$BR" -bufsize "$BR" \
        -c:a aac -b:a 128k \
        -f mpegts -muxrate "$MUXRATE" -mpegts_tstd_mode 1 \
        "$FINAL_TS" > "$FINAL_LOG" 2>&1

    "$VERIFIER" "$FINAL_LOG"
    if [ $? -eq 0 ]; then
        echo "[PASS] Bitrate $((BR/1000))k sweep successful"
    else
        echo "[FAIL] Bitrate $((BR/1000))k sweep failed"
    fi
done

# 2. GOP Sweep (Long GOP vs I-frame only)
echo ""
echo "[SWEEP] Testing GOP variants..."

declare -A GOP_MODES
GOP_MODES["iframe_only"]="-g 1"
GOP_MODES["long_gop"]="-g 250"
GOP_MODES["bframes"]="-g 50 -bf 2"

for MODE in "${!GOP_MODES[@]}"; do
    echo "[GOP] Testing: $MODE"
    FINAL_TS="$OUT_DIR/gop_${MODE}.ts"
    FINAL_LOG="$OUT_DIR/gop_${MODE}.log"

    $FFMPEG_BIN -y -v trace -i "$SRC" -t 5 \
        -c:v libx264 -b:v 2M ${GOP_MODES[$MODE]} \
        -c:a aac -b:a 128k \
        -f mpegts -muxrate 3000000 -mpegts_tstd_mode 1 \
        "$FINAL_TS" > "$FINAL_LOG" 2>&1

    "$VERIFIER" "$FINAL_LOG"
done

echo "===================================================="
echo "   Parameter Sweep Finished"
echo "===================================================="
