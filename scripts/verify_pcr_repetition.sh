#!/bin/bash
# TsAnalyzer: Professional PCR Diagnostic Verification

set -e
PORT_API=8088
PORT_UDP=19001
SAMPLE_TS="./sample/test_1m.ts"
[ ! -f "$SAMPLE_TS" ] && SAMPLE_TS="../sample/test_1m.ts"

echo ">>> TsAnalyzer: Diagnostic PCR Repetition Verification"

# 1. Clean Environment
pkill -9 tsa_cli || true
pkill -9 tsp || true
sleep 1

# 2. Run Analyzer WITH LOGS ENABLED (to stdout)
./build/tsa_cli --mode=live --udp $PORT_UDP 2>&1 | grep "CLOCK" &
CLI_PID=$!
sleep 2

# 3. Phase 1: High-frequency loops
echo ">>> Phase 1: Pushing stream with high-frequency loops..."
./build/tsp -P -l -b 20000000 -i 127.0.0.1 -p $PORT_UDP -f "$SAMPLE_TS" > /dev/null 2>&1 &
TSP_PID=$!
sleep 5

METRICS=$(curl -s http://localhost:$PORT_API/metrics)
BASE_ERR=$(echo "$METRICS" | grep "tsa_compliance_pcr_repetition_errors" | awk '{print $2}' || echo "0")

echo "------------------------------------------------------------"
echo "Baseline Errors: $BASE_ERR"
echo "------------------------------------------------------------"

# 4. Phase 2: real disruption
echo ">>> Phase 2: Simulating 200ms real gap (斷流)..."
kill -STOP $TSP_PID
sleep 0.5
kill -CONT $TSP_PID
sleep 3

FINAL_METRICS=$(curl -s http://localhost:$PORT_API/metrics)
ERR_COUNT=$(echo "$FINAL_METRICS" | grep "tsa_compliance_pcr_repetition_errors" | awk '{print $2}' || echo "0")

echo "------------------------------------------------------------"
echo "Final PCR Repetition Errors: $ERR_COUNT"
echo "------------------------------------------------------------"

kill -9 $CLI_PID $TSP_PID || true

if [ "$BASE_ERR" -eq 0 ] && [ "$ERR_COUNT" -eq 1 ]; then
    echo ">>> SUCCESS: Professional Deterministic PCR Measure Verified."
    exit 0
else
    echo ">>> FAILURE: Non-deterministic result. Check CLOCK logs above."
    exit 1
fi
