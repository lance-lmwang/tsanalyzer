#!/bin/bash
# TsAnalyzer Pro - 8-Stream Simulation (8Mbps) for High-Fidelity NOC Testing
# Priority: sample/test.ts > test.ts > sample/test_1m.ts

PORT=8088
BASE_UDP_PORT=19001

# Find the best available sample file
SAMPLE_FILE="./sample/test.ts"
[ ! -f "$SAMPLE_FILE" ] && SAMPLE_FILE="../sample/test.ts"
[ ! -f "$SAMPLE_FILE" ] && SAMPLE_FILE="/home/lmwang/dev/sample/test.ts"

function cleanup() {
    echo "=== [CLEANUP] Stopping TsAnalyzer and Data Pumps ==="
    fuser -k $PORT/tcp 2>/dev/null || true
    pkill -9 tsa_server 2>/dev/null || true
    pkill -9 tsp 2>/dev/null || true
    for i in {0..7}; do
        fuser -k -9 $((BASE_UDP_PORT + i))/udp >/dev/null 2>&1 || true
    done
    sleep 1
}

trap cleanup SIGINT SIGTERM

echo "=== [1/3] RESETTING ENVIRONMENT (PORT $PORT) ==="
cleanup

echo "=== [2/3] STARTING SERVER (PRO EDITION) ==="
./build/tsa_server_pro > server.log 2>&1 &
sleep 2

if ! curl -s http://localhost:$PORT/metrics > /dev/null; then
    echo "ERROR: Server failed to start on port $PORT"
    exit 1
fi

echo "=== [3/3] INJECTING 8 STREAMS USING $SAMPLE_FILE @ 8MBPS ==="
for i in {1..8}; do
    STREAM_ID="ST-$i"
    UDP_PORT=$((BASE_UDP_PORT + i - 1))
    BR=8000000 # 8 Mbps for better visualization

    echo "  -> $STREAM_ID on port $UDP_PORT (8 Mbps)"
    curl -s -X POST -H "Content-Type: application/json" -d "{\"stream_id\":\"$STREAM_ID\",\"url\":\"udp://127.0.0.1:$UDP_PORT\"}" "http://localhost:$PORT/api/v1/config/streams" > /dev/null
    nohup ./build/tsp -i 127.0.0.1 -p $UDP_PORT -l -f "$SAMPLE_FILE" -b $BR > /dev/null 2>&1 &
done

echo "===================================================="
echo "SIMULATION ACTIVE @ 8MBPS (PORT $PORT)"
echo "Sample File: $SAMPLE_FILE"
echo "NOC Dashboard: http://192.168.1.155:3000/big_screen_noc.html"
echo "===================================================="

while true; do
    echo "$(date) Health Check (8-Stream Wall):"
    curl -s "http://127.0.0.1:$PORT/metrics/core" | grep "tsa_system_signal_locked" | head -n 4
    sleep 5
done
