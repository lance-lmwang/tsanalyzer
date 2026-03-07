#!/bin/bash
# TsAnalyzer Pro - High-Concurrency Multi-Stream Stress Test (8-Stream @ 80Mbps Total)

BASE_UDP_PORT=19001
API_URL="http://localhost:8088/api/v1/streams"
SAMPLE_FILE="sample/test.ts"
TEST_DURATION=60
STREAMS=8

echo ">>> Initializing 8-Stream Performance Test..."

function cleanup() {
    echo ">>> Cleaning up processes..."
    pkill -9 tsa_server || true
    pkill -9 tsp || true
}
trap cleanup EXIT

# 1. Start Server
echo ">>> [Phase 1] Starting Server on Port 8088..."
./build/tsa_server > server_stress.log 2>&1 &
sleep 2

# 2. Register Streams via API
echo ">>> [Phase 2] Registering $STREAMS Dynamic Streams..."
for i in $(seq 1 $STREAMS); do
    PORT=$((BASE_UDP_PORT + i - 1))
    ID="STRESS_STREAM_$i"
    curl -s -X POST "$API_URL" -H "Content-Type: application/json" -d "{\"id\":\"$ID\"}" > /dev/null
    echo "    Registered $ID (UDP Target: $PORT)"
done

# 3. Inject Traffic
echo ">>> [Phase 3] Injecting 10Mbps Traffic per Stream (80Mbps Total)..."
for i in $(seq 1 $STREAMS); do
    PORT=$((BASE_UDP_PORT + i - 1))
    ./build/tsp -i 127.0.0.1 -p $PORT -l -f "$SAMPLE_FILE" -b 10000000 > /dev/null 2>&1 &
done

# 4. Monitoring Loop
echo ">>> [Phase 4] Monitoring Load & Internal Drops..."
echo "Time | Total Drops | Avg Health | Aggregated Bitrate"
echo "----------------------------------------------------"

for t in $(seq 5 5 $TEST_DURATION); do
    sleep 5
    METRICS=$(curl -s http://localhost:8088/metrics)

    # Aggregate stats across all streams
    DROPS=$(echo "$METRICS" | grep "tsa_internal_analyzer_drop" | awk '{sum+=$2} END {print sum}')
    HEALTH=$(echo "$METRICS" | grep "tsa_system_health_score" | awk '{sum+=$2; n++} END {if(n>0) print sum/n; else print 0}')
    BITRATE=$(echo "$METRICS" | grep "tsa_metrology_physical_bitrate_bps" | awk '{sum+=$2} END {printf "%.2f Mbps\n", sum/1000000}')

    printf "%3ds  | %11d | %10.1f | %s\n" $t "${DROPS:-0}" "$HEALTH" "$BITRATE"

    if [ "${DROPS:-0}" -gt 0 ]; then
        echo ">>> [WARNING] Internal packet drops detected!"
    fi
done

echo "----------------------------------------------------"
echo ">>> Stress Test Complete."
