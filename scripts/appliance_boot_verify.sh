#!/bin/bash
# TsAnalyzer Pro - APPLIANCE BOOT & VERIFICATION SUITE (v5.5.6)
# ------------------------------------------------------------------------------
set -e

# --- [0/5] FAST FAIL: SOURCE VALIDATION ---
PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$PROJECT_ROOT"

if [ -f "appliance.env" ]; then
    source appliance.env
else
    TSA_PORT=8088
    PROM_PORT=9090
fi

SAMPLE_FILE=${1:-sample/test.ts}

if [ ! -f "$SAMPLE_FILE" ]; then
    echo "================================================================================"
    echo "❌ FATAL ERROR: NO TRAFFIC SOURCE DETECTED"
    echo "================================================================================"
    echo "Search locations attempted: '$SAMPLE_FILE'"
    exit 1
fi
echo "🛡️  Source Verified: $SAMPLE_FILE"

echo "================================================================================"
echo "🛡️  TsAnalyzer Pro Appliance - SYSTEM INITIALIZATION"
echo "================================================================================"

echo "--- [1/5] CLEANING ENVIRONMENT & PORT CHECK ---"
if lsof -Pi :$TSA_PORT -sTCP:LISTEN -t >/dev/null ; then
    OCCUPYING_PROCESS=$(ps -p $(lsof -t -i:$TSA_PORT) -o comm= || echo "unknown")
    echo "⚠️  WARNING: Port $TSA_PORT is already in use by process: $OCCUPYING_PROCESS"
    exit 1
fi

pkill -9 tsa_server || true
pkill -9 tsp || true
fuser -k $TSA_PORT/tcp || true
docker compose -f monitoring/docker-compose.yml down --remove-orphans

# Network, Inference & Alignment
# Aligned with network_mode: host. Prometheus probes 127.0.0.1:$TSA_PORT directly.
sed -i "s/targets: .*/targets: ['127.0.0.1:$TSA_PORT']/g" monitoring/prometheus/prometheus.yml

echo "--- [3/5] UI BRANDING & DASHBOARD DEPLOY ---"
python3 scripts/deploy_dashboard.py

echo "--- [4/5] BOOTING CORE SERVICES ---"
cat > tsa.conf <<EOF
GLOBAL http_port $TSA_PORT
ST-1 udp://127.0.0.1:19001
ST-2 udp://127.0.0.1:19002
ST-3 udp://127.0.0.1:19003
ST-4 udp://127.0.0.1:19004
ST-5 udp://127.0.0.1:19005
ST-6 udp://127.0.0.1:19006
ST-7 udp://127.0.0.1:19007
ST-8 udp://127.0.0.1:19008
EOF

./build/tsa_server tsa.conf > server.log 2>&1 &
echo "Engine started (Port: $TSA_PORT)."

docker compose -f monitoring/docker-compose.yml up -d
echo "Monitoring stack started (Prometheus: $PROM_PORT, Grafana: 3000)."

echo "--- [5/5] TRAFFIC INJECTION (8 STREAMS @ 2MBPS) ---"
for i in {1..8}; do
    ./build/tsp -i 127.0.0.1 -p $((19000+i)) -l -f "$SAMPLE_FILE" -b 2000000 > /dev/null 2>&1 &
done

echo "Waiting for metrics to stabilize (20s)..."
sleep 20

echo "--- [FINAL] ACCEPTANCE TEST ---"
bash scripts/verify_appliance_integrity.sh
