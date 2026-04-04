#!/bin/bash
# TsAnalyzer Simple SRT Point-to-Point Test
# Pattern: tsa (Listener on 0.0.0.0) <- tsp (Caller to Physical IP)

IP="192.168.1.127"
PORT="9000"
BLUE='\033[34m'; GREEN='\033[32m'; RED='\033[31m'; NC='\033[0m'

echo -e "${BLUE}=== Starting Simple SRT Test ===${NC}"

# 1. Cleanup all
pkill tsa; pkill tsp; sleep 1

# 2. Start TSA analyzer (Listener mode, listening on all interfaces)
# Note: Use --mode=live ensure correct clock stepping
./build/tsa --srt-url "srt://:$PORT" --mode=live > tsa_simple.log 2>&1 &
TSA_PID=$!
echo -e "SRV: TSA listening on ${GREEN}srt://:$PORT${NC}"
sleep 2

# 3. Start TSP pump (Caller mode, target physical IP)
echo -e "TSP: Pushing cctv5.ts to ${GREEN}srt://$IP:$PORT${NC}"
./build/tsp -f ./sample/test.ts -b 10000000 --srt-url "srt://$IP:$PORT?mode=caller" -l > tsp_simple.log 2>&1 &
TSP_PID=$!

echo "WAIT: Transferring data for 15 seconds..."
sleep 15

# 4. Fetch results
BR=$(curl -s http://localhost:8088/metrics | grep "tsa_metrology_physical_bitrate_bps" | awk '{print $2}')
HE=$(curl -s http://localhost:8088/metrics | grep "tsa_system_health_score" | awk '{print $2}')

echo -e "${BLUE}--- Test Verdict ---${NC}"
if [[ ! -z "$BR" ]] && (( BR > 0 )); then
    echo -e "RESULT: ${GREEN}SUCCESS${NC}"
    echo -e "Detected Bitrate: ${GREEN}$BR bps${NC}"
    echo -e "Health Score: ${GREEN}$HE%${NC}"
    kill $TSA_PID $TSP_PID
    exit 0
else
    echo -e "RESULT: ${RED}FAILED${NC} (No traffic detected)"
    echo "Check tsa_simple.log and tsp_simple.log"
    kill $TSA_PID $TSP_PID
    exit 1
fi
