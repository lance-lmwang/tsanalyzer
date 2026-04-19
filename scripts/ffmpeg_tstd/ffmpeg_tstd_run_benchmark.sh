#!/bin/bash

ROOT_DIR=$(dirname $(dirname $(dirname $(readlink -f $0))))
OUT_DIR="${ROOT_DIR}/output"
mkdir -p "$OUT_DIR"

# Configuration parameters
TS_FILE="${ROOT_DIR}/../sample/cctvhd.ts"
[ ! -f "$TS_FILE" ] && TS_FILE="${ROOT_DIR}/sample/cctvhd.ts"
BITRATE=20000000
BIN="${ROOT_DIR}/build/tsp"
LOG_FILE="${OUT_DIR}/tstd_benchmark.log"
AUDITOR="${ROOT_DIR}/scripts/ffmpeg_tstd/tstd_telemetry_analyzer.py"
DURATION=30

echo "=== T-STD Toolchain Benchmark ==="
echo "Target Bitrate: $BITRATE bps"
echo "Duration: $DURATION seconds"
echo "Input: $TS_FILE"
echo "Log: $LOG_FILE"

# 1. Check permissions
echo "1. Requesting RT permissions (setcap)..."
setcap cap_sys_nice,cap_ipc_lock,cap_net_raw=ep "$BIN" 2>/dev/null || true

# 2. Launch real-time streaming
echo "2. Running benchmark for $DURATION..."
$BIN -i "$TS_FILE" -b $BITRATE -t $DURATION -v DEBUG > "$LOG_FILE" 2>&1

# 3. Analyze compliance
echo "3. Analyzing compliance results..."
if [ -f "$LOG_FILE" ]; then
    bash "$AUDITOR" "$LOG_FILE"
else
    echo "[ERROR] Benchmark log file not found."
    exit 1
fi

echo "------------------------------------------"
echo "Summary of results (last 5 lines):"
tail -n 5 "$LOG_FILE"
echo "------------------------------------------"
echo "Please return to the chat and tell me 'Benchmark Complete'."
