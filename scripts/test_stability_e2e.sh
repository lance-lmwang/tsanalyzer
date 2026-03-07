#!/bin/bash
# TsAnalyzer Pro: Mux Director Grade E2E Stability Test (5-Min UDP)

set -e
PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$PROJECT_ROOT"

PORT_API=8088
PORT_UDP=19001
SAMPLE="sample/test.ts"
DURATION=300

echo "--- STEP 1: Industrial Setup ---"
./build.sh > /dev/null
fuser -k 8088/tcp 8088/udp || true
sleep 1

echo "--- STEP 2: Launch Analyzer ---"
./build/tsa_server > server_stability.log 2>&1 &
SERVER_PID=$!
sleep 2

echo "--- STEP 3: Start PCR-Locked Pacer (tsp -P) ---"
taskset -c 1 ./build/tsp -P -l -t 7 -i 127.0.0.1 -p $PORT_UDP -f "$SAMPLE" > /dev/null 2>&1 &
TSP_PID=$!

echo "--- STEP 4: Real-time Monitor (300s) ---"
echo "Time(s) | Health | CC Errors | Bitrate (Mbps)"
echo "--------------------------------------------------"

START_TIME=$(date +%s)
PREV_CC=-1

while true; do
    CURRENT_TIME=$(date +%s)
    ELAPSED=$((CURRENT_TIME - START_TIME))

    if [ $ELAPSED -ge $DURATION ]; then
        break
    fi

    METRICS=$(curl -s http://localhost:8088/metrics || echo "")
    if [ -n "$METRICS" ]; then
        HEALTH=$(echo "$METRICS" | grep "tsa_system_health_score" | awk '{print $2}')
        CC=$(echo "$METRICS" | grep "tsa_compliance_tr101290_p1_cc_errors_total" | awk '{print $2}')
        BPS=$(echo "$METRICS" | grep "tsa_metrology_physical_bitrate_bps" | awk '{print $2}')
        MBPS=$(echo "scale=2; $BPS / 1000000" | bc)

        printf "[%3ds] | %6.1f | %9d | %6.2f Mbps
" "$ELAPSED" "$HEALTH" "$CC" "$MBPS"

        if [ "$PREV_CC" != "-1" ] && [ "$CC" -gt "$PREV_CC" ]; then
            echo "[FAIL] FAILED: CC Error Increment detected!"
            kill $SERVER_PID $TSP_PID || true
            exit 1
        fi
        PREV_CC=$CC
    else
        echo "Waiting for metrics..."
    fi

    sleep 5
done

echo "--------------------------------------------------"
echo "[PASS] SUCCESS: 5-Minute Stability Verified (UDP + PCR-Locked)"
kill $SERVER_PID $TSP_PID || true
exit 0
