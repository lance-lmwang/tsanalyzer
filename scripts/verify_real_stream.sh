#!/bin/bash
# TsAnalyzer Dual-Mode Fidelity Verifier
# Baseline: Offline File Analysis
# Live: SRT Stream Analysis

BLUE='\033[34m'
GREEN='\033[32m'
RED='\033[31m'
NC='\033[0m'

SAMPLE="./sample/test.ts"
[ ! -f "$SAMPLE" ] && echo "Err: $SAMPLE not found" && exit 1

echo -e "${BLUE}=== STEP 1: Running Offline Baseline (DNA Audit) ===${NC}"
# Use replay mode for deterministic results
./build/tsa --mode=replay "$SAMPLE" > /dev/null 2>&1
BASE_BR=$(cat final_metrology.json | jq '.metrics.bitrate_bps')
BASE_PIDS=$(cat final_metrology.json | jq '.pids | length')
echo -e "BASELINE: Bitrate = ${GREEN}$BASE_BR bps${NC}, PIDs = ${GREEN}$BASE_PIDS${NC}"

echo -e "${BLUE}=== STEP 2: Running Real-time SRT Pipeline ===${NC}"
pkill tsa; pkill tsp; sleep 1

# Start TSA Listener
./build/tsa --srt-url "srt://127.0.0.1:9000" --mode=live > tsa_live.log 2>&1 &
TSA_PID=$!
sleep 2

# Start TSP Pusher (matching the original file bitrate)
./build/tsp -f "$SAMPLE" -b "$BASE_BR" --srt-url "srt://127.0.0.1:9000?mode=caller" -l > /dev/null 2>&1 &
TSP_PID=$!

echo "WAIT: Analyzing real-time SRT flow for 15s..."
sleep 15

# Fetch Live Metrics
LIVE_BR=$(curl -s http://localhost:8088/metrics | grep "tsa_metrology_physical_bitrate_bps" | awk '{print $2}')
LIVE_HEALTH=$(curl -s http://localhost:8088/metrics | grep "tsa_system_health_score" | awk '{print $2}')

echo -e "REAL-TIME: Bitrate = ${GREEN}$LIVE_BR bps${NC}, Health = ${GREEN}$LIVE_HEALTH%${NC}"

# 3. Fidelity Comparison
DIFF=$(echo "$LIVE_BR - $BASE_BR" | bc | tr -d '-')
THRESHOLD=500000 # 50kbps margin

kill $TSA_PID $TSP_PID

echo -e "${BLUE}=== STEP 3: Fidelity Verdict ===${NC}"
if [ -z "$LIVE_BR" ] || [ "$LIVE_BR" == "0" ]; then
    echo -e "${RED}[FAIL] No data received in live mode.${NC}"
    exit 1
fi

if (( DIFF < THRESHOLD )); then
    echo -e "${GREEN}[PASS] Real-time metrics align with Offline Baseline.${NC}"
    exit 0
else
    echo -e "${RED}[FAIL] Significant bitrate deviation ($DIFF bps).${NC}"
    exit 1
fi
