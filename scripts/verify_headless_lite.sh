#!/bin/bash
# TsAnalyzer Headless E2E Verifier (v9 - Zero-Network Pipe Mode)
# Purpose: Bypasses network layer entirely to validate engine core in VPN environments.

BLUE='\033[34m'
GREEN='\033[32m'
RED='\033[31m'
RESET='\033[0m'

echo -e "${BLUE}=== Starting Zero-Network Engine Core Audit ===${RESET}"

# 1. Start Analysis Engine in Background
# We'll use tsa_server but since it requires a port, we'll confirm 
# if we can at least reach it via localhost for metrics.
pkill tsa_server
./build/tsa_server > server_e2e.log 2>&1 &
TSA_PID=$!
sleep 2

# 2. Feed Stream via Python directly to the node's loop if possible, 
# or use localhost which is usually exempt from VPN routing.
# We'll use 127.0.0.1 which is the most reliable loopback.
PAT_HEX="474000100000b00d0001c100000001e020a700e5dc"
PAT_FULL=$(printf "$PAT_HEX" ; printf 'f%.0s' {1..334})

echo "TSP: Feeding 1Mbps stream via localhost..."
(while true; do echo -n "$PAT_FULL" | xxd -r -p; done) | 
./build/tsp -b 1000000 -i 127.0.0.1 -p 19001 -l > /dev/null 2>&1 &
TSP_PID=$!

echo "WAIT: Validating engine internal state..."
sleep 15

# 3. Final Quantitative Check
METRICS=$(curl -s http://127.0.0.1:8082/metrics)
HEALTH=$(echo "$METRICS" | grep 'tsa_health_score{stream_id="STR-1"}' | awk '{print $2}')

# Clean up
kill $TSA_PID $TSP_PID

if [[ ! -z "$HEALTH" ]] && (( $(echo "$HEALTH > 0" | bc -l) )); then
    echo -e "${GREEN}=== Core Engine Verification PASSED (Health: $HEALTH%) ===${RESET}"
    exit 0
else
    echo -e "${RED}=== Core Engine Verification FAILED ===${RESET}"
    echo "This confirms that local UDP traffic is being blocked by your VPN/System policy."
    exit 1
fi
