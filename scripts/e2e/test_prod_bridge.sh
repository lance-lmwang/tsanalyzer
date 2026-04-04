#!/bin/bash
# TsAnalyzer Production Bridge Test (SRT-to-UDP)
# Logic: SRT_MONITOR tools -> SRT -> UDP -> TSA_SERVER

RED='\033[0;31m'; GREEN='\033[0;32m'; BLUE='\033[0;34m'; NC='\033[0m'

echo -e "${BLUE}=== Starting Production-Grade Bridge Test ===${NC}"

# 1. Start TSA Server (Metrics at 8088)
pkill tsa_server; pkill srt-live-transmit; pkill -f "tsp"
echo "CCTV5 udp://127.0.0.1:19001" > tsa.conf
./build/tsa_server tsa.conf > server_prod.log 2>&1 &
TSA_PID=$!

# 2. Start SRT-to-UDP Bridge (using srt_monitor assets)
# This acts as the 'SRT Server' you mentioned
echo "INFRA: Launching SRT-to-UDP Bridge on :10080..."
../../srt_monitor/bin/srt-live-transmit srt://:10080 udp://127.0.0.1:19001 -v > bridge.log 2>&1 &
BRIDGE_PID=$!
sleep 2

# 3. Start Pushing real cctv5.ts (via SRT Caller)
echo "TSP: Pushing cctv5.ts to SRT Bridge..."
./build/tsp -f ./sample/test.ts -b 10000000 --srt-url "srt://127.0.0.1:10080?mode=caller" -l > /dev/null 2>&1 &
TSP_PID=$!

echo "WAIT: Waiting 15s for analysis lock..."
sleep 15

# 4. Final Audit
echo -e "${BLUE}=== Analyzing Live Telemetry ===${NC}"
METRICS=$(curl -s http://localhost:8088/metrics)
HEALTH=$(echo "$METRICS" | grep 'tsa_system_health_score{stream_id="CCTV5"}' | awk '{print $2}')
BITRATE=$(echo "$METRICS" | grep 'tsa_metrology_physical_bitrate_bps{stream_id="CCTV5"}' | awk '{print $2}')

echo -e "RESULT: CCTV5 Health: ${GREEN}$HEALTH%${NC}"
echo -e "RESULT: CCTV5 Bitrate: ${GREEN}$BITRATE bps${NC}"

# Cleanup
kill $TSA_PID $BRIDGE_PID $TSP_PID
if [[ ! -z "$BITRATE" ]] && (( BITRATE > 5000000 )); then
    echo -e "${GREEN}=== Production Bridge Test PASSED ===${NC}"
    exit 0
else
    echo -e "${RED}=== Production Bridge Test FAILED ===${NC}"
    exit 1
fi
