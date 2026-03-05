#!/bin/bash
# TsAnalyzer Pro: 5-Min Production Soak Test (7 Streams)
PORT_API=8100
STREAMS=7
DURATION=300
SAMPLE="../sample/test.ts"

echo "--- [SOAK TEST] v14.0 Production Stability (300s) ---"
ulimit -n 65535
fuser -k -9 $PORT_API/tcp || true
for i in {0..6}; do fuser -k -9 $((12345 + i))/udp || true; done
pkill -9 tsa_server || true; pkill -9 tsp || true; sleep 2

./build/tsa_server > /tmp/server_soak.log 2>&1 &
SERVER_PID=$!
sleep 3

for i in {0..6}; do
    taskset -c $((i+1)) ./build/tsp -P -l -t 7 -i 127.0.0.1 -p $((12345 + i)) -f "$SAMPLE" > /dev/null 2>&1 &
done

echo "Calibrating baseline (60s pre-warm)..."
sleep 60
BASE_CC=$(curl -s http://localhost:$PORT_API/metrics | grep "tsa_cc" | awk '{sum+=$2} END {print sum}')
echo "Baseline CC Total: $BASE_CC"

START=$(date +%s)
while true; do
    ELAPSED=$(($(date +%s) - START))
    if [ $ELAPSED -ge $DURATION ]; then break; fi

    CUR_CC=$(curl -s http://localhost:$PORT_API/metrics | grep "tsa_cc" | awk '{sum+=$2} END {print sum}')
    if [ "$CUR_CC" -gt "$BASE_CC" ]; then
        echo "❌ FAILURE: CC Leakage detected! (+$((CUR_CC - BASE_CC)))"
        kill -9 $SERVER_PID $(pgrep tsp) && exit 1
    fi
    printf "[%3ds] Total CC: %d | Status: STABLE
" "$ELAPSED" "$CUR_CC"
    sleep 20
done

echo "✅ FINAL VERDICT: 5-MIN STABILITY PASSED (CC INCREMENT: 0)"
kill -9 $SERVER_PID $(pgrep tsp) || true
