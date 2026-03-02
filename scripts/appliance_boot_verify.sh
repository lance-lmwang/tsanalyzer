#!/bin/bash
# TsAnalyzer Pro - APPLIANCE BOOT & VERIFICATION SUITE (v5.5.4)
# ------------------------------------------------------------------------------
set -e

# --- [0/5] FAST FAIL: SOURCE VALIDATION ---
PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

if [ -n "$1" ]; then
    SAMPLE_FILE="$1"
else
    # 自动探测逻辑
    SAMPLE_FILE=$(ls $PROJECT_ROOT/sample/*.ts 2>/dev/null | head -n 1)
fi

# 核心判定：如果没有找到文件，立即退出，不执行后续清理
if [ -z "$SAMPLE_FILE" ] || [ ! -f "$SAMPLE_FILE" ]; then
    echo "================================================================================"
    echo "❌ FATAL ERROR: NO TRAFFIC SOURCE DETECTED"
    echo "================================================================================"
    echo "TsAnalyzer requires a .ts file to generate metrics."
    echo "Search locations attempted:"
    echo "  1. Command line argument: '$1'"
    echo "  2. Default directory: $PROJECT_ROOT/sample/*.ts"
    echo ""
    echo "Please place a sample file in 'sample/' or provide a path:"
    echo "Usage: $0 <path_to_file.ts>"
    echo "================================================================================"
    exit 1
fi

echo "🛡️  Source Verified: $SAMPLE_FILE"

echo "================================================================================"
echo "🛡️  TsAnalyzer Pro Appliance - SYSTEM INITIALIZATION"
echo "================================================================================"


echo "--- [1/5] CLEANING ENVIRONMENT & PORT CHECK ---"
# Check if port 8082 is occupied by another process (excluding our own cleanup target)
if lsof -Pi :8082 -sTCP:LISTEN -t >/dev/null ; then
    OCCUPYING_PROCESS=$(ps -p $(lsof -t -i:8082) -o comm= || echo "unknown")
    echo "⚠️  WARNING: Port 8082 is already in use by process: $OCCUPYING_PROCESS"
    echo "Please free port 8082 or modify 'tsa.conf' and scripts before proceeding."
    exit 1
fi

pkill -9 tsa_server || true
pkill -9 tsp || true
fuser -k 8082/tcp || true
docker compose -f monitoring/docker-compose.yml down -v

# Network, Inference & Alignment
# Aligned with network_mode: host. Prometheus probes 127.0.0.1:8082 directly.
sed -i "s/targets: .*/targets: ['127.0.0.1:8082']/g" monitoring/prometheus/prometheus.yml

echo "--- [3/5] UI BRANDING & DASHBOARD DEPLOY ---"
python3 scripts/deploy_dashboard.py

echo "--- [4/5] BOOTING CORE SERVICES ---"
# Create a fresh tsa.conf using 8082
cat > tsa.conf <<EOF
GLOBAL http_port 8082
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
docker compose -f monitoring/docker-compose.yml up -d
sleep 8

echo "--- [5/5] TRAFFIC INJECTION (8 STREAMS @ 2MBPS) ---"
for i in {1..8}; do
    nohup ./build/tsp -i 127.0.0.1 -p $((19000+i)) -l -f "$SAMPLE_FILE" -b 2000000 > /dev/null 2>&1 &
done

echo "Waiting for metrics to stabilize (20s)..."
sleep 20

echo "--- [FINAL] ACCEPTANCE TEST ---"
bash scripts/verify_appliance_integrity.sh
