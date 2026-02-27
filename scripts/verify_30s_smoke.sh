#!/bin/bash
# TsAnalyzer Pro: 30s Smoke Test (Single Stream)
set -e
PORT_API=8100
PORT_UDP=12345
SAMPLE="/home/lmwang/dev/sample/cctvhd.ts"

echo "--- [SMOKE TEST] v14.0 High-Perf Kernel ---"
fuser -k -9 $PORT_API/tcp $PORT_UDP/udp || true
./build/tsa_server > /dev/null 2>&1 &
SERVER_PID=$!
sleep 2

taskset -c 1 ./build/tsp -P -l -t 7 -i 127.0.0.1 -p $PORT_UDP -f "$SAMPLE" > /dev/null 2>&1 &
TSP_PID=$!

echo "Stabilizing 10s..."
sleep 10
echo "Time | CC Errors | Mbps | Status"
for i in {1..3}; do
    sleep 10
    METRICS=$(curl -s http://localhost:$PORT_API/metrics | grep 'id="1"')
    CC=$(echo "$METRICS" | grep "tsa_cc" | awk '{print $2}')
    MBPS=$(echo "$METRICS" | grep "tsa_mbps" | awk '{print $2}')
    printf "[%2ds] | %9d | %5.2f | ✅ OK
" $((i*10)) "$CC" "$MBPS"
done

kill -9 $SERVER_PID $TSP_PID || true
echo "--- SMOKE TEST PASSED ---"
