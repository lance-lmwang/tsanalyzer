#!/bin/bash
# TsAnalyzer: Professional PCR Diagnostic Verification

set -e
PORT_API=8088
PORT_UDP=19001

# Select best available sample
SAMPLE_TS="./sample/mpts_4prog.ts"
if [ ! -f "$SAMPLE_TS" ]; then
    SAMPLE_TS="./sample/mpts_4prog.ts"
fi
[ ! -f "$SAMPLE_TS" ] && SAMPLE_TS="../sample/mpts_4prog.ts"

echo ">>> Using sample: $SAMPLE_TS"
echo ">>> TsAnalyzer: Diagnostic PCR Repetition Verification"

# 1. Clean Environment
pkill -9 tsa_cli || true
pkill -9 tsp || true
sleep 1

# 2. Phase 1: High-frequency PCR (baseline)
./build/tsa_cli --mode=live --udp $PORT_UDP 2>&1 | grep "PCR_TRACK" &
CLI_PID=$!
sleep 1

./build/tsp -P -l -b 20000000 -i 127.0.0.1 -p $PORT_UDP -f "$SAMPLE_TS" > /dev/null 2>&1 &
TSP_PID=$!

# Let it stabilize
sleep 5

# 3. Phase 1: check baseline
FINAL_METRICS=$(curl -s http://127.0.0.1:$PORT_API/json)
BASE_ERR=$(echo "$FINAL_METRICS" | grep -oP '"tsa_compliance_pcr_repetition_errors":\s*\K\d+' | head -n 1 || echo "0")

echo "------------------------------------------------------------"
echo "Baseline Errors: $BASE_ERR"
echo "------------------------------------------------------------"

# 4. Phase 2: real disruption
echo ">>> Phase 2: Simulating 200ms real gap..."
kill -STOP $TSP_PID
sleep 0.5
kill -CONT $TSP_PID
sleep 3

# 5. Check if error count increased
FINAL_METRICS=$(curl -s http://127.0.0.1:$PORT_API/json)
ERR_COUNT=$(echo "$FINAL_METRICS" | grep -oP '"tsa_compliance_pcr_repetition_errors":\s*\K\d+' | head -n 1 || echo "0")

echo "------------------------------------------------------------"
echo "Final PCR Repetition Errors: $ERR_COUNT"
echo "------------------------------------------------------------"

kill -9 $CLI_PID $TSP_PID > /dev/null 2>&1 || true

if [ "$ERR_COUNT" -gt "$BASE_ERR" ]; then
    echo ">>> SUCCESS: Professional Deterministic PCR Measure Verified."
    exit 0
else
    echo ">>> FAILURE: Non-deterministic result. Check CLOCK logs above."
    exit 1
fi
