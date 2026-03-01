#!/bin/bash
# TsAnalyzer Industrial Remote Monitoring with EXTERNAL CHAOS PROXY
# Stack: TSP -> ChaosProxy (2000x) -> TSA_SERVER (1900x) -> Grafana

IP="192.168.1.127"
BLUE='\033[34m'
GREEN='\033[32m'
RED='\033[31m'
RESET='\033[0m'

echo -e "${BLUE}=== Starting Monitoring Stack with External Chaos Support ===${RESET}"

# 1. Build
make release

# 2. Start TSA Server (Production Mode)
echo "SRV: Starting TSA Analysis Engine..."
pkill tsa_server
./build/tsa_server > tsa_server.log 2>&1 &
TSA_PID=$!

# 3. Start Chaos Proxy in background
echo "CHAOS: Starting External Chaos Proxy..."
pkill -f chaos_proxy.py
python3 scripts/chaos_proxy.py > chaos_proxy.log 2>&1 &
PROXY_PID=$!

# 4. Start TSP Fleet via Proxy Ports (20001-20004)
echo "TSP: Starting 4-stream simulation fleet via Proxy..."
pkill tsp
for i in {1..4}; do
    PROXY_PORT=$((20000 + i))
    # We send to the proxy port
    ./build/tsp -b 5000000 -i 127.0.0.1 -p $PROXY_PORT -l > /dev/null 2>&1 &
    echo "  - Stream #$i: 5Mbps -> Proxy:$PROXY_PORT"
done

echo -e "${GREEN}=== System is LIVE ===${RESET}"
echo -e "NOC Dashboard: ${BLUE}http://$IP:3000${RESET}"
echo -e "Metrics API:   ${BLUE}http://$IP:8080/metrics${RESET}"
echo ""
echo "TO SIMULATE FAULTS:"
echo "Run: 'python3 scripts/chaos_proxy.py' and type 'drop STR-2 0.1'"

trap "echo 'Shutting down...'; kill $TSA_PID; kill $PROXY_PID; pkill tsp; exit" SIGINT
wait
