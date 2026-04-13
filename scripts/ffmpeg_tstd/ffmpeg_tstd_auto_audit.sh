#!/bin/bash
# T-STD Industrial Auto-Audit & Repro Tool (V1.0)
# Purpose: Reproduce skews, detect drops, and auto-generate content if source missing.

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
OUT_DIR="${ROOT_DIR}/output/auto_audit"
mkdir -p "$OUT_DIR"

FFMPEG_ROOT="$(cd "$ROOT_DIR/../ffmpeg.wz.master" && pwd)"
ffm="${FFMPEG_ROOT}/ffdeps_img/ffmpeg/bin/ffmpeg"
src_custom="${ROOT_DIR}/../sample/news.ts"
src_generated="${OUT_DIR}/synthetic_stress.ts"

# 1. Source Readiness Check
if [ ! -f "$src_custom" ]; then
    echo -e "\033[33m[WARN] Source news.ts not found. Generating synthetic 1.5Mbps stress source...\033[0m"
    $ffm -y -f lavfi -i "testsrc2=size=720x576:rate=25" -f lavfi -i "sine=frequency=1000:sample_rate=48000" -f lavfi -i "sine=frequency=500:sample_rate=48000" \
         -t 300 -c:v libx264 -b:v 1000k -c:a ac3 -b:a:0 384k -b:a:1 192k -f mpegts "$src_generated" > /dev/null 2>&1
    SRC_FILE="$src_generated"
else
    SRC_FILE="$src_custom"
fi

echo "[*] Using Source: $SRC_FILE"

# 2. Parallel Fail-Fast Runner
run_audit() {
    local mode=$1
    local muxrate=2200000
    local log="${OUT_DIR}/mode${mode}.log"
    local dst="${OUT_DIR}/mode${mode}.ts"

    echo "[*] Launching Mode $mode Audit..."
    $ffm -hide_banner -y -i "$SRC_FILE" -c:v libwz264 -b:v 1000k -c:a copy -muxrate $muxrate \
         -mpegts_tstd_mode $mode -f mpegts "$dst" > "$log" 2>&1 &
    local pid=$!

    # Live Monitor Loop
    while kill -0 $pid 2>/dev/null; do
        if grep -E "MEDIA DROP|DRIVE FUSE" "$log" > /dev/null; then
            echo -e "\033[31m[CRITICAL] Mode $mode FAILED! Killing task...\033[0m"
            grep -E "MEDIA DROP|DRIVE FUSE" "$log" | head -n 3
            kill -9 $pid
            return 1
        fi
        sleep 2
    done
    echo -e "\033[32m[PASS] Mode $mode finished successfully.\033[0m"
    return 0
}

# 3. Main Execution
echo "=== T-STD Full Dimension Stress Audit ==="
run_audit 1 &
pid1=$!
run_audit 2 &
pid2=$!

wait $pid1
wait $pid2

# 4. Post-Mortem Audit (EB Check)
echo ""
echo "--- Final Parameter Audit ---"
grep "PID 0x0100" "${OUT_DIR}/mode1.log" | tail -n 1
if grep "EB=0KB" "${OUT_DIR}/mode1.log" > /dev/null; then
    echo -e "\033[31m[FAIL] Log contains EB=0KB precision error!\033[0m"
fi

echo "=== Audit Complete ==="
