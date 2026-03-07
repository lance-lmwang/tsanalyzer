#!/bin/bash

# TsPacer/TsAnalyzer SRT End-to-End Test Script

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$PROJECT_ROOT"

# Configuration
SRT_PORT=9000
SAMPLE_FILE="./sample/test.ts"
[ ! -f "$SAMPLE_FILE" ] && SAMPLE_FILE="../sample/test.ts"
[ ! -f "$SAMPLE_FILE" ] && SAMPLE_FILE="/home/lmwang/dev/sample/test.ts"
METRICS_URL="http://localhost:8000/metrics"

echo "=== [1/5] Building Project ==="
./build.sh
if [ $? -ne 0 ]; then echo "Build failed!"; exit 1; fi

# Cleanup previous instances
pkill tsa || true
pkill tsp || true
sleep 1

echo "=== [2/5] Launching TsAnalyzer (SRT Listener) ==="
# Listener on port 9000
nohup ./build/tsa --srt-url "srt://:9000" > analyzer_srt.log 2>&1 &
ANALYZER_PID=$!
echo "$ANALYZER_PID" > .e2e_analyzer.pid
echo "Analyzer started (PID: $ANALYZER_PID). Waiting for listener init..."
sleep 2

echo "=== [3/5] Launching TsPacer (SRT Caller) ==="
# Caller to 127.0.0.1:9000
# -P: PCR pacing
# -l: Loop
# --srt-url: SRT destination
nohup ./build/tsp -P --srt-url "srt://127.0.0.1:9000" -l -f "$SAMPLE_FILE" > pacer_srt.log 2>&1 &
PACER_PID=$!
echo "$PACER_PID" > .e2e_pacer.pid
echo "Pacer started (PID: $PACER_PID)."

echo "=== [4/5] Verifying Data Flow (Running for 60s) ==="
sleep 10
METRICS=$(curl -s "$METRICS_URL")
if echo "$METRICS" | grep -q "tsa"; then
    echo "SUCCESS: Metrics are being exported!"
    # Check bitrate to verify data flow
    echo "Metric Check:"
    echo "$METRICS" | grep "muxrate" || echo "Warning: No muxrate found"
    echo "$METRICS" | grep "physical_bitrate" || echo "Warning: No physical_bitrate found"
    echo "SRT Metrics:"
    echo "$METRICS" | grep "srt_" || echo "Warning: No SRT metrics found"
else
    echo "FAILURE: Could not fetch metrics from $METRICS_URL."
    echo "=== Analyzer Log ==="
    head -n 20 analyzer_srt.log
    echo "=== Pacer Log ==="
    head -n 20 pacer_srt.log
    exit 1
fi

echo "-------------------------------------------------------"
echo "SRT E2E TEST RUNNING"
echo "Logs: tail -f analyzer_srt.log"
echo "-------------------------------------------------------"
