#!/bin/bash
# TsAnalyzer Industrial Remote Monitoring Launcher - PHYSICAL IP FIX
# Full Stack: TSP x 8 -> TSA_SERVER (Physical IP) -> Prometheus -> Grafana

IP="192.168.1.127"
BLUE='\033[34m'
GREEN='\033[32m'
RED='\033[31m'
RESET='\033[0m'

echo -e "${BLUE}=== Starting Industrial Remote Monitoring Stack (IP: $IP) ===${RESET}"

# 1. Build
make release

# 2. Start TSA Server
echo "SRV: Starting TSA Analysis Engine..."
pkill tsa_server
./build/tsa_server > tsa_server.log 2>&1 &
TSA_PID=$!

# 3. Start TSP Fleet using Physical IP
echo "TSP: Starting 4-stream simulation fleet via $IP..."
pkill tsp
for i in {1..4}; do
    PORT=$((19000 + i))
    ./build/tsp -b 5000000 -i $IP -p $PORT -l > /dev/null 2>&1 &
    echo "  - Stream #$i: 5Mbps -> $IP:$PORT"
done

echo -e "${GREEN}=== Remote Monitoring Stack is LIVE ===${RESET}"
echo -e "Access NOC Dashboard: ${BLUE}http://$IP:3000${RESET}"
echo -e "Access Metrics API:  ${BLUE}http://$IP:8080/metrics${RESET}"

trap "echo 'Shutting down...'; kill $TSA_PID; pkill tsp; exit" SIGINT
wait
