#!/bin/bash
# TsAnalyzer Pro - 30s Big Screen Monitoring Test

SAMPLE_FILE="/tmp/dummy.ts"
if [ ! -f "$SAMPLE_FILE" ]; then
    dd if=/dev/zero bs=188 count=10000 > "$SAMPLE_FILE"
    for i in {0..9999}; do printf '\x47' | dd of="$SAMPLE_FILE" bs=1 seek=$((i*188)) conv=notrunc 2>/dev/null; done
fi

function cleanup() {
    pkill -9 tsa_server
    pkill -9 tsp
}
trap cleanup EXIT

echo "=== [1/2] Starting metrics server (Port 8088) ==="
./build/tsa_server > server.log 2>&1 &
sleep 2

echo "=== [2/2] Injecting 8-stream HD set (10 Mbps each) ==="
for i in {1..8}; do
    UDP_PORT=$((19001 + i - 1))
    ./build/tsp -i 127.0.0.1 -p $UDP_PORT -l -f "$SAMPLE_FILE" -b 10000000 > /dev/null 2>&1 &
done

echo "----------------------------------------------------"
echo "Monitoring active! You can access: http://localhost:8088/metrics"
echo "Simulating big-screen data fetching (duration 30s)..."
echo "----------------------------------------------------"

for i in {1..15}; do
    echo "[$(date +%H:%M:%S)] Scrape test..."
    curl -s http://localhost:8088/metrics | grep "tsa_system_total_packets" | head -n 4
    sleep 2
done

echo "Test ended."
