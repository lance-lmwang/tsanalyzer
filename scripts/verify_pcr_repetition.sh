#!/bin/bash
# TsAnalyzer: PCR Repetition & Heartbeat E2E Verification
# Validates TR 101 290 1.1 (40ms) using the lightweight CLI.

set -e
PORT_API=8088
PORT_UDP=19001
SAMPLE_TS="./sample/test_1m.ts"
[ ! -f "$SAMPLE_TS" ] && SAMPLE_TS="../sample/test_1m.ts"
[ ! -f "$SAMPLE_TS" ] && SAMPLE_TS="/home/lmwang/dev/sample/test_1m.ts"

echo ">>> TsAnalyzer: PCR Repetition Verification (via CLI)"

# 0. Check dependencies
if [ ! -f "$SAMPLE_TS" ]; then
    echo "[FAIL] ERROR: Sample TS not found."
    exit 1
fi

# 1. Start CLI in live mode
echo ">>> Phase 1: Starting Lightweight Analyzer..."
pkill -9 tsa_cli || true
pkill -9 tsp || true
./build/tsa_cli --mode=live --udp $PORT_UDP > /dev/null 2>&1 &
CLI_PID=$!
sleep 2

# 2. Push valid stream
echo ">>> Phase 2: Pushing valid stream (20Mbps)..."
./build/tsp -P -l -b 20000000 -i 127.0.0.1 -p $PORT_UDP -f "$SAMPLE_TS" > /dev/null 2>&1 &
TSP_PID=$!
sleep 5

# 3. Verify Signal Lock
echo ">>> Waiting for Signal Lock..."
MAX_RETRIES=15
for i in $(seq 1 $MAX_RETRIES); do
    METRICS=$(curl -s http://localhost:$PORT_API/metrics || echo "")
    if echo "$METRICS" | grep -q 'tsa_system_signal_locked.* 1'; then
        echo "    [PASS] Signal Locked"
        break
    fi
    sleep 1
done

if [ "$i" -eq "$MAX_RETRIES" ]; then
    echo "    [FAIL] FAILED: Signal not locked after $MAX_RETRIES seconds"
    kill -9 $CLI_PID $TSP_PID || true
    exit 1
fi

# 4. Simulate PCR Gap (TR 101 290 1.1)
echo ">>> Phase 3: Simulating 200ms PCR Gap (Stream Pause)..."
# Get baseline errors
BASE_ERR=$(echo "$METRICS" | grep "tsa_compliance_pcr_repetition_errors" | head -n 1 | awk '{print $2}' || echo "0")

kill -STOP $TSP_PID
sleep 1.0
kill -CONT $TSP_PID
sleep 5

# 5. Check Error Counter
FINAL_METRICS=$(curl -s http://localhost:$PORT_API/metrics)
ERR_COUNT=$(echo "$FINAL_METRICS" | grep "tsa_compliance_pcr_repetition_errors" | head -n 1 | awk '{print $2}' || echo "0")

echo "------------------------------------------------------------"
echo "PCR Repetition Errors (Baseline: $BASE_ERR): $ERR_COUNT"
echo "------------------------------------------------------------"

kill -9 $CLI_PID $TSP_PID > /dev/null 2>&1 || true

if [ "$ERR_COUNT" -gt "$BASE_ERR" ]; then
    echo ">>> PCR COMPLIANCE VERIFIED: Alpha-Beta & Heartbeat active."
    exit 0
else
    echo ">>> PCR COMPLIANCE FAILED: No new error reported for 200ms gap."
    exit 1
fi
