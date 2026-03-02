#!/bin/bash
# TsAnalyzer Automated Chaos Simulation Verifier (v3 - High Fidelity)
# Logic: Valid CC Generator -> ChaosProxy -> TSA_SERVER -> Detection

BLUE='\033[34m'
GREEN='\033[32m'
RED='\033[31m'
RESET='\033[0m'

echo -e "${BLUE}=== Starting Chaos Simulation Test (High Fidelity) ===${RESET}"

# 1. Cleanup
pkill tsa_server
pkill -f chaos_proxy.py
pkill -f "tsp"
echo '{"drop_rates": {"19001": 0.0, "19002": 0.0}}' > chaos_config.json
sleep 1

# 2. Start Components
./build/tsa_server > server.log 2>&1 &
TSA_PID=$!
python3 scripts/chaos_proxy.py > proxy.log 2>&1 &
PROXY_PID=$!
sleep 2

# 3. Start High-Fidelity Generator
# Generates TS packets with PID 0x100 and incrementing CC (0-15)
python3 -c "
import sys, time
cc = 0
header_base = b'\x47\x01\x00\x10' # Sync, PID 0x100, Payload only
while True:
    header = header_base[0:3] + bytes([header_base[3] | (cc & 0x0F)])
    pkt = header + b'\xff' * 184
    sys.stdout.buffer.write(pkt)
    cc += 1
    if cc % 100 == 0: sys.stdout.buffer.flush()
" | ./build/tsp -b 2000000 -i 127.0.0.1 -p 20002 -l > /dev/null 2>&1 &
GEN_PID=$!

echo "INFRA: High-fidelity 2Mbps generator active (STR-2 PID:0x100)"

# 4. Wait for engine to baseline
echo "WAIT: Waiting 10s for engine lock..."
sleep 10

# 5. Inject 30% Packet Loss (Aggressive to ensure score drop)
echo -e "${RED}CHAOS: Injecting 30% packet loss to STR-2...${RESET}"
echo '{"drop_rates": {"19002": 0.30}}' > chaos_config.json
sleep 15

# 6. Analysis
METRICS=$(curl -s http://localhost:8082/metrics)
CC_ERR=$(echo "$METRICS" | grep 'tsa_tr101290_p1_cc_error{stream_id="STR-2"}' | awk '{print $2}')
HEALTH=$(echo "$METRICS" | grep 'tsa_health_score{stream_id="STR-2"}' | awk '{print $2}')

echo -e "RESULT: STR-2 CC Errors: ${GREEN}$CC_ERR${RESET}"
echo -e "RESULT: STR-2 Health Score: ${GREEN}$HEALTH${RESET}"

# 7. Cleanup
kill $TSA_PID $PROXY_PID $GEN_PID
pkill -f "tsp"

# 8. Evaluation
if (( $(echo "$CC_ERR > 20" | bc -l) )) && (( $(echo "$HEALTH < 80" | bc -l) )); then
    echo -e "${GREEN}=== Chaos Simulation Test PASSED ===${RESET}"
    exit 0
else
    echo -e "${RED}=== Chaos Simulation Test FAILED ===${RESET}"
    echo "Potential Cause: Engine logic or data reachability."
    exit 1
fi
