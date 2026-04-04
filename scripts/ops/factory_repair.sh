#!/bin/bash
# TsAnalyzer Pro - Factory Repair Script (v5.5.1)
set -e

echo "--- [1/3] HARD RESET ---"
pkill -9 tsa_server || true
pkill -9 tsp || true
fuser -k 8088/tcp || true

echo "--- [2/3] STARTING ENGINE ---"
./build/tsa_server tsa.conf > server.log 2>&1 &
sleep 5

echo "--- [3/3] INJECTING TRAFFIC ---"
for i in {1..8}; do
    nohup ./build/tsp -i 127.0.0.1 -p $((19000+i)) -l -f sample/test.ts -b 2000000 > /dev/null 2>&1 &
done

echo "Waiting for metrics to stabilize..."
for i in {1..10}; do
    if curl -s http://localhost:8088/metrics | grep -q "tsa_system_health_score"; then
        echo "[PASS] Metrics detected!"
        bash scripts/verify_appliance_integrity.sh
        exit 0
    fi
    echo "Attempt $i: No metrics yet..."
    sleep 3
done

echo "[FAIL] ERROR: Metrics never appeared. Check server.log"
tail -n 20 server.log
exit 1
