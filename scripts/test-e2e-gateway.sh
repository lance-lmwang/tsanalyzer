#!/bin/bash

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$PROJECT_ROOT"

# Configuration
SRT_PORT=9001
SAMPLE_FILE="/home/lmwang/dev/sample/cctvhd.ts"
METRICS_URL="http://localhost:8001/metrics"

echo "=== [1/4] Building Project ==="
./build.sh || exit 1

# Cleanup
pkill tsg || true
pkill tsp || true
sleep 1

echo "=== [2/4] Launching TsGateway (SRT Listener) ==="
# SRT Listener, CC Repair enabled, PCR Restamp enabled
nohup ./build/tsg --srt-in "srt://:9001" --repair-cc --repair-pcr --dest-ip 127.0.0.1 --dest-port 1237 --bitrate 10000000 > gateway.log 2>&1 &
GW_PID=$!
echo "Gateway started (PID: $GW_PID)"
sleep 2

echo "=== [3/4] Launching TsPacer (SRT Caller) ==="
# Push file to gateway via SRT
nohup ./build/tsp -P --srt-url "srt://127.0.0.1:9001" -l -f "$SAMPLE_FILE" > pacer.log 2>&1 &
PACER_PID=$!
echo "Pacer started (PID: $PACER_PID)"

echo "=== [4/4] Verifying Data Flow ==="
sleep 10
METRICS=$(curl -s "$METRICS_URL")
if echo "$METRICS" | grep -q "tsa"; then
    echo "SUCCESS: Gateway is exporting metrics!"
    echo "Metrology Sample:"
    echo "$METRICS" | grep "muxrate"
    echo "$METRICS" | grep "continuity_errors"

    # Check if action engine mitigations are visible if we can
    # (Actually we'd need to induce errors to see them in action)
else
    echo "FAILURE: Gateway metrics not found at $METRICS_URL"
    tail -n 20 gateway.log
    exit 1
fi

echo "Test passed. Cleaning up..."
pkill tsg || true
pkill tsp || true
exit 0
