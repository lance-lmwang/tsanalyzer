#!/bin/bash
# TsAnalyzer Pro - 8-Stream Simulation (2Mbps) for Grafana

PORT=8082
BASE_UDP_PORT=19001
SAMPLE_FILE="sample/test.ts"

function cleanup() {
    echo "Stopping..."
    pkill -9 tsa_server
    pkill -9 tsp
    exit 0
}
trap cleanup SIGINT SIGTERM

echo "=== [1/3] RESETTING ENVIRONMENT ==="
fuser -k $PORT/tcp 2>/dev/null || true
pkill -9 tsa_server 2>/dev/null || true
pkill -9 tsp 2>/dev/null || true
sleep 1

echo "=== [2/3] STARTING SERVER ==="
./build/tsa_server http://0.0.0.0:$PORT > server.log 2>&1 &
sleep 2

echo "=== [3/3] INJECTING 8 STREAMS AT 2MBPS ==="
for i in {1..8}; do
    STREAM_ID="ST-$i"
    UDP_PORT=$((BASE_UDP_PORT + i - 1))
    BR=2000000

    echo "  -> $STREAM_ID on port $UDP_PORT (2 Mbps)"
    curl -s -X POST -H "Content-Type: application/json" -d "{\"stream_id\":\"$STREAM_ID\",\"url\":\"udp://127.0.0.1:$UDP_PORT\"}" "http://localhost:$PORT/api/v1/config/streams" > /dev/null
    nohup ./build/tsp -i 127.0.0.1 -p $UDP_PORT -l -f "$SAMPLE_FILE" -b $BR > /dev/null 2>&1 &
done

echo "===================================================="
echo "SIMULATION ACTIVE @ 2MBPS"
echo "Prometheus Target: http://192.168.7.2:8082/metrics"
echo "===================================================="

while true; do
    echo "$(date) Health Check:"
    curl -s "http://127.0.0.1:$PORT/metrics" | grep "tsa_system_total_packets" | head -n 4
    sleep 5
done
