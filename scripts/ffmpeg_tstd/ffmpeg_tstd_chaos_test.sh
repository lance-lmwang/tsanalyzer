#!/bin/bash
# T-STD Chaos/Resilience Test Suite
# Purpose: Validate T-STD engine behavior against mutated/corrupt input timing.

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
OUT_DIR="${ROOT_DIR}/output/chaos"
mkdir -p "$OUT_DIR"

# --- Toolchain Discovery ---
FFMPEG_ROOT="$(cd "$ROOT_DIR/../ffmpeg.wz.master" 2>/dev/null && pwd)"
if [ -z "$FFMPEG_ROOT" ] || [ ! -x "$FFMPEG_ROOT/ffdeps_img/ffmpeg/bin/ffmpeg" ]; then
    FFMPEG_BIN=$(which ffmpeg)
else
    FFMPEG_BIN="$FFMPEG_ROOT/ffdeps_img/ffmpeg/bin/ffmpeg"
fi

MUTATOR="$ROOT_DIR/scripts/tools/ts_mutator.py"
VERIFIER="$SCRIPT_DIR/ffmpeg_tstd_verify_compliance.sh"

SRC="/home/lmwang/sample/input.mp4"
[ ! -f "$SRC" ] && SRC="/home/lmwang/sample/input.mp4" # Fallback check

BITRATE_KB=2000
MUXRATE=25000000
DURATION=10

echo "===================================================="
echo "   T-STD CHAOS RESILIENCE TEST SUITE (v1.0)"
echo "===================================================="
echo "[*] FFmpeg: $FFMPEG_BIN"
echo "[*] Source: $SRC"
echo "[*] Output: $OUT_DIR"

# 1. Generate Baseline TS
echo "[Phase 1] Generating Clean Baseline TS..."
BASE_TS="$OUT_DIR/baseline.ts"
$FFMPEG_BIN -y -i "$SRC" -t $DURATION \
    -c:v libx264 -b:v ${BITRATE_KB}k -preset ultrafast \
    -c:a aac -b:a 128k \
    -f mpegts -muxrate $MUXRATE "$BASE_TS" > /dev/null 2>&1

if [ ! -f "$BASE_TS" ]; then
    echo "[ERROR] Failed to generate baseline TS"
    exit 1
fi

# 2. Define Chaos Matrix
declare -a MODES=("pcr_jump" "pcr_jitter" "video_jump" "audio_lag")

# 3. Execution Loop
for MODE in "${MODES[@]}"; do
    echo ""
    echo "----------------------------------------------------"
    echo "[CHAOS] Testing Mode: $MODE"
    echo "----------------------------------------------------"

    MUTATED_TS="$OUT_DIR/mutated_${MODE}.ts"
    FINAL_TS="$OUT_DIR/final_${MODE}.ts"
    FINAL_LOG="$OUT_DIR/final_${MODE}.log"

    # Step A: Mutate
    echo "[*] Injecting Fault: $MODE..."
    python3 "$MUTATOR" "$BASE_TS" "$MUTATED_TS" "$MODE" > /dev/null

    # Step B: Re-encode through T-STD Engine
    echo "[*] Processing through T-STD Engine..."
    $FFMPEG_BIN -y -v trace -i "$MUTATED_TS" \
        -c:v copy -c:a copy \
        -f mpegts \
        -muxrate $MUXRATE -mpegts_tstd_mode 1 \
        "$FINAL_TS" > "$FINAL_LOG" 2>&1

    # Step C: Verify Compliance
    echo "[*] Verifying Final Output..."
    "$VERIFIER" "$FINAL_LOG"
    if [ $? -eq 0 ]; then
        echo "[PASS] T-STD Engine successfully handled $MODE"
    else
        echo "[FAIL] T-STD Engine failed to normalize $MODE"
        # We don't exit here to continue other tests
    fi
done

echo ""
echo "===================================================="
echo "   Chaos Test Suite Finished"
echo "===================================================="
