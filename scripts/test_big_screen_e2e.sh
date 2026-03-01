#!/bin/bash
# TsAnalyzer Headless E2E Verifier (v14 - Logic Validation)
# Purpose: Final standalone logic verification using internal data pump.

BLUE='\033[34m'
GREEN='\033[32m'
RED='\033[31m'
RESET='\033[0m'

echo -e "${BLUE}=== Starting Standalone Logic E2E Audit ===${RESET}"

# 1. Cleanup
pkill tsa_server
sleep 1

# 2. Start TSA Server (Simulation/Pump Mode)
./build/tsa_server > server_e2e.log 2>&1 &
TSA_PID=$!
echo "SRV: Internal Data Pump active."

# 3. Wait for baseline (Stable scores)
echo "WAIT: Waiting 10s for metrology baseline..."
sleep 10

# 4. Audit
METRICS=$(curl -s http://localhost:8080/metrics)
FAILED=0
for i in {1..4}; do
    STR="STR-$i"
    HEALTH=$(echo "$METRICS" | grep "tsa_health_score{stream_id=\"$STR\"}" | awk '{print $2}')
    
    echo -ne "  - $STR: "
    if [[ -z "$HEALTH" ]] || (( $(echo "$HEALTH < 90" | bc -l) )); then
        echo -e "${RED}[FAIL] Health: $HEALTH%${RESET}"
        FAILED=1
    else
        echo -e "${GREEN}[PASS] Health: $HEALTH%${RESET}"
    fi
done

# 5. Cleanup
kill $TSA_PID

if [ $FAILED -eq 0 ]; then
    echo -e "${GREEN}=== Standalone E2E Test PASSED ===${RESET}"
    exit 0
else
    echo -e "${RED}=== Standalone E2E Test FAILED ===${RESET}"
    exit 1
fi
