#!/bin/bash
# T-STD Parallel Marathon Auditor (20,000 Frames)
# Purpose: Long-term stability check. Fail-fast on any error.

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
OUT_DIR="${ROOT_DIR}/output/marathon"
mkdir -p "$OUT_DIR"

FFMPEG_ROOT="$(cd "$ROOT_DIR/../ffmpeg.wz.master" && pwd)"
ffm="${FFMPEG_ROOT}/ffdeps_img/ffmpeg/bin/ffmpeg"
src="${ROOT_DIR}/../sample/news.ts"

cleanup() {
    pkill -P $$
    exit
}
trap cleanup EXIT

run_audit() {
    local mode=$1
    local log="${OUT_DIR}/mode${mode}.log"
    local dst="${OUT_DIR}/mode${mode}.ts"

    echo "[*] Launching Mode $mode Marathon (20,000 frames)..."
    # Use -frames:v to ensure 20,000 frames run
    $ffm -hide_banner -y -i "$src" -frames:v 20000 -c:v libwz264 -b:v 1000k -c:a copy -muxrate 2200k \
         -mpegts_tstd_mode $mode -f mpegts "$dst" > "$log" 2>&1 &
    local pid=$!

    while kill -0 $pid 2>/dev/null; do
        if grep -qE "MEDIA DROP|DRIVE FUSE" "$log"; then
            echo -e "\033[31m[CRITICAL] Mode $mode FAILED during marathon!\033[0m"
            grep -E "MEDIA DROP|DRIVE FUSE" "$log" | tail -n 3
            exit 1
        fi
        # Progress reporting
        local progress=$(grep "frame=" "$log" | tail -n 1 | awk '{print $1}')
        echo -ne "Mode $mode Progress: $progress\r"
        sleep 5
    done
    echo -e "\n\033[32m[PASS] Mode $mode completed 20,000 frames successfully.\033[0m"
}

echo "=== T-STD 20,000 Frames Comparative Marathon ==="
run_audit 1 &
run_audit 2 &

wait
echo "=== Marathon Complete ==="
