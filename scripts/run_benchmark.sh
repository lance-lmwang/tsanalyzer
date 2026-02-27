#!/bin/bash

# Configuration parameters
TS_FILE="/home/lmwang/dev/cctvhd.ts"
BITRATE=20000000
BIN="./build/tsp"
LOG_FILE="benchmark_results.log"
DURATION="60s"

echo "=== TsPacer CBR 60s Benchmark ==="

# 1. Attempt to grant RT permissions
echo "1. Requesting RT permissions (setcap)..."
sudo setcap cap_sys_nice,cap_ipc_lock,cap_net_raw=ep "$BIN"

if [ $? -ne 0 ]; then
    echo "Error: Failed to set permissions. Please run with sudo access."
    exit 1
fi

# 2. Launch real-time streaming
echo "2. Running benchmark for $DURATION..."
echo "   Bitrate: $BITRATE bps"
echo "   Core: 0"
echo "   Log: $LOG_FILE"

# Use timeout to ensure precise execution time
timeout $DURATION "$BIN" -b $BITRATE -i 127.0.0.1 -p 1234 -c 0 -f "$TS_FILE" > "$LOG_FILE" 2>&1

echo "3. Benchmark finished."
echo "------------------------------------------"
echo "Summary of results (last 5 lines):"
tail -n 5 "$LOG_FILE"
echo "------------------------------------------"
echo "Please return to the chat and tell me 'Benchmark Complete'."
