#!/bin/bash
# TsAnalyzer Single-Stream Robustness Test
# Logic: tsp -> 127.0.0.1:19001 -> tsa_server -> metrics check

BLUE='\033[34m'
GREEN='\033[32m'
RED='\033[31m'
RESET='\033[0m'

echo -e "${BLUE}=== Starting Single-Stream Robustness Test ===${RESET}"

# 1. Cleanup
pkill tsa_server
pkill -f "gen_golden_stream.py"
pkill -f "tsp"
sleep 1

# 2. Start TSA Server (UDP Mode)
# We will use the production binary
./build/tsa_server > server_single.log 2>&1 &
TSA_PID=$!
echo "SRV: Analysis engine started (PID: $TSA_PID)."

# 3. Start 1 Golden Generator via Loopback
echo "GEN: Starting 1 golden stream to 127.0.0.1:19001..."
python3 scripts/gen_golden_stream.py 19001 > /dev/null 2>&1 &
GEN_PID=$!

echo "WAIT: Waiting 15s for analysis lock..."
sleep 15

# 4. Audit Logic
METRICS=$(curl -s http://localhost:8080/metrics)
STR="STR-1"
HEALTH=$(echo "$METRICS" | grep "tsa_health_score{stream_id="$STR"}" | awk '{print $2}')
BITRATE=$(echo "$METRICS" | grep "tsa_physical_bitrate_bps{stream_id="$STR"}" | awk '{print $2}')

echo -e "${BLUE}=== Final Audit Results ===${RESET}"
echo -e "Stream: $STR"
echo -e "Health: ${GREEN}$HEALTH%${RESET}"
echo -e "Bitrate: ${GREEN}$BITRATE bps${RESET}"

# 5. Cleanup
kill $TSA_PID $GEN_PID

if [[ ! -z "$HEALTH" ]] && (( $(echo "$HEALTH > 90" | bc -l) )); then
    echo -e "${GREEN}=== Single-Stream Test PASSED ===${RESET}"
    exit 0
else
    echo -e "${RED}=== Single-Stream Test FAILED ===${RESET}"
    echo "Check server_single.log for details."
    exit 1
fi
