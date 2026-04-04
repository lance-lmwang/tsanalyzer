#!/bin/bash
# TsAnalyzer Pro - Appliance Factory Reset & Verification (v5.5.1)
# Ensures a clean, working 8-stream NOC state from scratch.

set -e

echo "--- [1/6] CLEANING PREVIOUS STATE ---"
pkill -9 tsa_server || true
pkill -9 tsp || true
rm -f monitoring/grafana/provisioning/dashboards/*.json
rm -f server.log pacer.log simulation.log server_run.log pacer_run.log
bash monitoring/monitoring-purge.sh --all

echo "--- [2/6] GENERATING APPLIANCE CONFIG (8 STREAMS) ---"
cat > tsa.conf <<EOF
GLOBAL http_port 8088
ST-1 udp://127.0.0.1:19001
ST-2 udp://127.0.0.1:19002
ST-3 udp://127.0.0.1:19003
ST-4 udp://127.0.0.1:19004
ST-5 udp://127.0.0.1:19005
ST-6 udp://127.0.0.1:19006
ST-7 udp://127.0.0.1:19007
ST-8 udp://127.0.0.1:19008
EOF

echo "--- [3/6] STARTING ENGINE & MONITORING ---"
./build/tsa_server tsa.conf > server.log 2>&1 &
bash monitoring/monitoring-up.sh

echo "--- [4/6] DEPLOYING THREE-PLANE DASHBOARDS ---"
python3 scripts/deploy_dashboard.py

echo "--- [5/6] INJECTING 8-STREAM TRAFFIC (10s WARMUP) ---"
for i in {1..8}; do
    nohup ./build/tsp -i 127.0.0.1 -p $((19000+i)) -l -f sample/test.ts -b 2000000 > /dev/null 2>&1 &
done
sleep 15

echo "--- [6/6] FINAL METRICS VERIFICATION ---"
STATUS=$(curl -s http://localhost:8088/metrics | grep "tsa_system_health_score" | wc -l)

if [ "$STATUS" -ge 8 ]; then
    echo "[PASS] SUCCESS: $STATUS streams reporting health metrics (Includes 8 simulation + local-probes)."
    echo "Dashboard: http://localhost:3000/d/global-wall"
else
    echo "[FAIL] FAILURE: Only $STATUS/8 streams reporting."
    echo "Check server.log for details."
    exit 1
fi
