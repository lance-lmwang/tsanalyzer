#!/bin/bash
# TsAnalyzer Big Screen E2E Test
# Logic: TSP -> TSA_SERVER -> JSON API Validation

BLUE='\033[34m'
GREEN='\033[32m'
RED='\033[31m'
RESET='\033[0m'

echo -e "${BLUE}=== Starting Big Screen E2E Test ===${RESET}"

# 1. Build
make release

# 2. Cleanup old processes
pkill tsa_server
pkill tsp
sleep 1

# 3. Start TSA Server in background
./build/tsa_server > tsa_server.log 2>&1 &
TSA_PID=$!
echo -e "SRV: TSA Server started (PID: $TSA_PID), listening on HTTP 8080"

# 4. Start TSP to send stream
# Target: 5Mbps CBR stream to UDP 19001 (Stream 1)
./build/tsp -b 5000000 -o udp://127.0.0.1:19001 > tsp.log 2>&1 &
TSP_PID=$!
echo -e "TSP: Sending 5Mbps CBR stream to TSA (PID: $TSP_PID)"

# 5. Wait for engine to lock and baseline
echo "Waiting 5s for metrology engine to stabilize..."
sleep 5

# 6. Automatic Data Validation
echo -e "${BLUE}=== Validating JSON API Data ===${RESET}"
JSON_DATA=$(curl -s http://localhost:8080/api/snapshot)

if [[ -z "$JSON_DATA" ]]; then
    echo -e "${RED}FAILURE: API returned no data!${RESET}"
    exit 1
fi

BITRATE=$(echo $JSON_DATA | grep -oP '"bitrate_bps":\s*\K[0-9]+')
HEALTH=$(echo $JSON_DATA | grep -oP '"master_health":\s*\K[0-9.]+')

echo -e "METRIC: Detected Bitrate: ${GREEN}$BITRATE bps${RESET}"
echo -e "METRIC: Master Health: ${GREEN}$HEALTH%${RESET}"

# Validation logic: 5Mbps +/- 10%
if (( BITRATE > 4500000 && BITRATE < 5500000 )); then
    echo -e "${GREEN}SUCCESS: Bitrate validation passed (approx 5Mbps).${RESET}"
else
    echo -e "${RED}FAILURE: Bitrate out of expected range!${RESET}"
    kill $TSA_PID $TSP_PID
    exit 1
fi

if (( $(echo "$HEALTH > 90" | bc -l) )); then
    echo -e "${GREEN}SUCCESS: Health validation passed (>90%).${RESET}"
else
    echo -e "${RED}FAILURE: Health score too low!${RESET}"
    kill $TSA_PID $TSP_PID
    exit 1
fi

echo -e "${GREEN}=== Big Screen Test PASSED ===${RESET}"
echo "You can now open mock_noc_dashboard.html in your browser to see live data."
echo "Press Ctrl+C to stop the test and cleanup."

# Keep running for manual observation
wait
