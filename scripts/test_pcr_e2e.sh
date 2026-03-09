#!/bin/bash
set -e

PORT_API=8088  # tsa_cli default
PORT_UDP=19001
SAMPLE_TS="./sample/mpts_4prog.ts"
if [ ! -f "$SAMPLE_TS" ]; then
    SAMPLE_TS="sample/mpts_4prog.ts"
fi

echo ">>> E2E PCR Verification via tsa_cli (Live Mode)"

# 0. Check sample file
if [ ! -f "$SAMPLE_TS" ]; then
    echo "[FAIL] FATAL ERROR: Sample file '$SAMPLE_TS' not found."
    exit 1
fi

# 1. Cleanup
echo ">>> Cleaning up..."
pkill -9 tsa_cli || true
pkill -9 tsp || true
sleep 1

# 2. Start tsa_cli in LIVE mode
echo ">>> Starting tsa_cli on UDP $PORT_UDP..."
./build/tsa_cli --mode=live --udp $PORT_UDP &
CLI_PID=$!
sleep 5 # Increased sleep to ensure bind

# Verify bind
if ! ss -unlp | grep "$PORT_UDP" | grep -q "tsa_cli"; then
    echo "[FAIL] FAILED: tsa_cli failed to bind to UDP $PORT_UDP"
    kill -9 $CLI_PID || true
    exit 1
fi

# 3. Start Pacer (Fast pace)
echo ">>> Phase 1: Sending stream (50Mbps)..."
./build/tsp -P -l -b 50000000 -i 127.0.0.1 -p $PORT_UDP -f "$SAMPLE_TS" > /dev/null 2>&1 &
TSP_PID=$!
sleep 5

# 4. Wait for Lock
echo ">>> Waiting for metrics to populate..."
MAX_RETRIES=15
for i in $(seq 1 $MAX_RETRIES); do
    METRICS=$(curl -s http://localhost:$PORT_API/metrics || echo "")
    LOCK=$(echo "$METRICS" | grep "^tsa_system_signal_locked" | awk '{print $2}' || echo "0")
    if [ "$LOCK" == "1" ]; then
        echo "    [PASS] Signal locked"
        break
    fi
    sleep 1
done

if [ "$i" -eq "$MAX_RETRIES" ]; then
    echo "[FAIL] FAILED: Timeout waiting for signal lock."
    kill -9 $CLI_PID $TSP_PID || true
    exit 1
fi

# 5. Simulate Interruption
echo ">>> Phase 2: Simulating 200ms gap (TR 101 290 1.1 Trigger)..."
kill -STOP $TSP_PID
sleep 1.0
kill -CONT $TSP_PID
sleep 5

# 6. Verify Error Count
FINAL_METRICS=$(curl -s http://localhost:$PORT_API/metrics)
# Match either the specific label one or the generic one if available
ERR_COUNT=$(echo "$FINAL_METRICS" | grep "tsa_compliance_pcr_repetition_errors" | head -n 1 | awk '{print $2}' || echo "0")

echo "------------------------------------------------------------"
echo "Final PCR Repetition Error Count: $ERR_COUNT"
echo "------------------------------------------------------------"

if [ "$ERR_COUNT" != "0" ]; then
    echo "[PASS] SUCCESS: Alpha-Beta Clock & Timeout logic verified!"
    kill -9 $CLI_PID $TSP_PID || true
    exit 0
else
    echo "[FAIL] FAILED: PCR Timeout was NOT recorded in metrics."
    kill -9 $CLI_PID $TSP_PID || true
    exit 1
fi
