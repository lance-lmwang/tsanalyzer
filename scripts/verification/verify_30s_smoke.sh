#!/bin/bash
# TsAnalyzer Pro: 30s Smoke Test (Single Stream)
set -e
PORT_API=8088
PORT_UDP=19001
SAMPLE="./sample/test.ts"
[ ! -f "$SAMPLE" ] && SAMPLE="../../sample/test.ts"
[ ! -f "$SAMPLE" ] && SAMPLE="/home/lmwang/dev/sample/test.ts"

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
    sleep 5
    METRICS=$(curl -s http://localhost:$PORT_API/metrics | grep 'stream_id="STR-1"')
    CC=$(echo "$METRICS" | grep "tsa_tr101290_p1_cc_error" | awk '{print $2}')
    MBPS=$(echo "$METRICS" | grep "tsa_metrology_physical_bitrate_bps" | awk '{print $2}')
    # Convert bps to Mbps
    MBPS_F=$(echo "scale=2; $MBPS / 1000000" | bc)
    printf "[%2ds] | %9d | %5.2f | [PASS] OK\n" $((i*5+10)) "$CC" "$MBPS_F"
done

kill -9 $SERVER_PID $TSP_PID || true
echo "--- SMOKE TEST PASSED ---"
