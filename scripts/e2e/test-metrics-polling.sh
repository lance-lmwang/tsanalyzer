#!/bin/bash

# Test script to verify metrics updates every 5 seconds (Prometheus style)

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$PROJECT_ROOT"

UDP_PORT=1235
SAMPLE_FILE="./sample/test.ts"
[ ! -f "$SAMPLE_FILE" ] && SAMPLE_FILE="../../sample/test.ts"
[ ! -f "$SAMPLE_FILE" ] && SAMPLE_FILE="/home/lmwang/dev/sample/test.ts"
METRICS_URL="http://localhost:8000/metrics"

# 1. Start tsa
echo "Starting tsa..."
./build/tsa --udp $UDP_PORT > analyzer_test.log 2>&1 &
ANALYZER_PID=$!

# 2. Start tsp to feed data (PCR-locked mode)
echo "Starting tsp in PCR mode..."
./build/tsp -P -i 127.0.0.1 -p $UDP_PORT -l -f "$SAMPLE_FILE" > pacer_test.log 2>&1 &
PACER_PID=$!

function cleanup() {
    echo "Cleaning up..."
    kill $ANALYZER_PID $PACER_PID 2>/dev/null
    exit
}

trap cleanup SIGINT SIGTERM

echo "Polling metrics every 5 seconds (5 rounds)..."
for i in {1..5}
do
    echo "Round $i/5:"
    sleep 5
    METRICS=$(curl -s "$METRICS_URL")

    # Check for core metrics
    CONTINUITY=$(echo "$METRICS" | grep "tsa_continuity_errors_total")
    MUXRATE=$(echo "$METRICS" | grep "tsa_muxrate_kbps")
    PHYSICAL=$(echo "$METRICS" | grep "tsa_physical_bitrate_kbps")
    PACKETS=$(echo "$METRICS" | grep "tsa_pid_bitrate_kbps")

    echo "  $CONTINUITY"
    echo "  $MUXRATE"
    echo "  $PHYSICAL"

    if [[ -z "$MUXRATE" || "$MUXRATE" == *" 0.00"* ]]; then
        echo "  [FAIL] Muxrate is empty or zero"
    else
        echo "  [PASS] Metrics are populating"
    fi
    echo "-----------------------------------"
done

cleanup
