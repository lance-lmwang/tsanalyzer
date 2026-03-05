#!/bin/bash
# TsAnalyzer Network IO Verifier (UDP & SRT)
# Purpose: Final acceptance test for point-to-point CLI transmission.

BLUE='\033[34m'
GREEN='\033[32m'
RED='\033[31m'
NC='\033[0m'

SAMPLE="../sample/test.ts"
[ ! -f "$SAMPLE" ] && echo "Err: $SAMPLE not found" && exit 1

function run_test() {
    PROTO=$1
    CMD_TSA=$2
    CMD_TSP=$3
    
    echo -e "${BLUE}=== Testing $PROTO Transmission ===${NC}"
    pkill tsa; pkill tsp; sleep 1
    
    # 1. Start TSA in background
    $CMD_TSA > tsa_test.log 2>&1 &
    TSA_PID=$!
    sleep 2
    
    # 2. Start TSP pushing real data
    $CMD_TSP > /dev/null 2>&1 &
    TSP_PID=$!
    
    echo "WAIT: Transferring data for 15s..."
    sleep 15
    
    # 3. Probe Metrics
    METRICS=$(curl -s http://localhost:12345/metrics)
    BR=$(echo "$METRICS" | grep "tsa_physical_bitrate_bps" | awk '{print $2}')
    HE=$(echo "$METRICS" | grep "tsa_health_score" | awk '{print $2}')
    
    # 4. Cleanup
    kill $TSA_PID $TSP_PID > /dev/null 2>&1
    
    # 5. Verdict
    if [[ ! -z "$BR" ]] && (( BR > 1000000 )); then
        echo -e "RESULT: $PROTO Bitrate: ${GREEN}$BR bps${NC}, Health: ${GREEN}$HE%${NC}"
        return 0
    else
        echo -e "RESULT: $PROTO ${RED}FAILED${NC} (No data received)"
        return 1
    fi
}

# --- TEST 1: UDP ---
run_test "UDP" 
    "./build/tsa --udp 19001 --mode=live" 
    "./build/tsp -f $SAMPLE -b 10000000 -i 127.0.0.1 -p 19001 -l"
UDP_RES=$?

echo ""

# --- TEST 2: SRT ---
run_test "SRT" 
    "./build/tsa --srt-url srt://:9000 --mode=live" 
    "./build/tsp -f $SAMPLE -b 10000000 --srt-url srt://127.0.0.1:9000?mode=caller -l"
SRT_RES=$?

echo "--------------------------------------------------"
if [ $UDP_RES -eq 0 ] && [ $SRT_RES -eq 0 ]; then
    echo -e "${GREEN}ALL NETWORK IO TESTS PASSED${NC}"
    exit 0
else
    echo -e "${RED}SOME NETWORK IO TESTS FAILED${NC}"
    exit 1
fi
