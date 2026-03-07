#!/bin/bash
# TsAnalyzer Pro - 8-Stream Simulation (2Mbps) for Grafana
# Standardized Port: 8088

PORT=8088
BASE_UDP_PORT=19001
SAMPLE_FILE="sample/test_1m.ts"

function cleanup() {
    echo "=== [CLEANUP] Stopping TsAnalyzer and Data Pumps ==="
    # Force cleanup of 8088 port
    fuser -k $PORT/tcp 2>/dev/null || true
    # Cleanup tsa_server, tsp and related processes
    pkill -9 tsa_server 2>/dev/null || true
    pkill -9 tsp 2>/dev/null || true
    # Cleanup UDP port range
    for i in {0..7}; do
        fuser -k -9 $((BASE_UDP_PORT + i))/udp >/dev/null 2>&1 || true
    done
    sleep 1
}

# Trap signals (SIGINT, SIGTERM), exclude EXIT to keep background pumps alive
trap cleanup SIGINT SIGTERM

echo "=== [1/3] RESETTING ENVIRONMENT (PORT $PORT) ==="
cleanup

echo "=== [2/3] STARTING SERVER (PRO EDITION) ==="
# Force use of Pro binary
./build/tsa_server_pro > server.log 2>&1 &
sleep 2

# Check if service started
if ! curl -s http://localhost:$PORT/metrics > /dev/null; then
    echo "ERROR: Server failed to start on port $PORT"
    exit 1
fi

echo "=== [3/3] INJECTING 8 STREAMS AT 2MBPS ==="
if [ ! -f "$SAMPLE_FILE" ]; then
    echo "WARNING: $SAMPLE_FILE not found, searching for alternatives..."
    SAMPLE_FILE=$(ls sample/*.ts | head -n 1)
fi

for i in {1..8}; do
    STREAM_ID="ST-$i"
    UDP_PORT=$((BASE_UDP_PORT + i - 1))
    BR=2000000

    echo "  -> $STREAM_ID on port $UDP_PORT (2 Mbps)"
    # Use new API and metric prefixes
    curl -s -X POST -H "Content-Type: application/json" -d "{\"stream_id\":\"$STREAM_ID\",\"url\":\"udp://127.0.0.1:$UDP_PORT\"}" "http://localhost:$PORT/api/v1/config/streams" > /dev/null
    nohup ./build/tsp -i 127.0.0.1 -p $UDP_PORT -l -f "$SAMPLE_FILE" -b $BR > /dev/null 2>&1 &
done

echo "===================================================="
echo "SIMULATION ACTIVE @ 2MBPS (PORT $PORT)"
echo "Prometheus Target: http://localhost:$PORT/metrics"
echo "NOC Dashboard: big_screen_noc.html"
echo "===================================================="

while true; do
    echo "$(date) Health Check (New Metrics):"
    # Use new metric names for heartbeat
    curl -s "http://127.0.0.1:$PORT/metrics" | grep "tsa_system_signal_locked" | head -n 4
    sleep 5
done
