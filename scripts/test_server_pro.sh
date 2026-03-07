#!/bin/bash
# test_server_pro.sh - High Concurrency Test for TsAnalyzer Pro

SRT_PORT=9000
UDP_PORT=19001
HTTP_PORT=8088
SAMPLE_FILE="./sample/test.ts"
BLUE='\033[34m'; GREEN='\033[32m'; RED='\033[31m'; NC='\033[0m'

echo -e "${BLUE}=== Starting TsAnalyzer Pro Server Test ===${NC}"

# 1. Cleanup
pkill -9 tsa_server_pro
pkill -9 tsp
sleep 1

# 2. Start Pro Server
stdbuf -oL ./build/tsa_server_pro $SRT_PORT > server_pro.log 2>&1 &
SERVER_PID=$!
echo -e "SRV: Pro Server started on SRT:$SRT_PORT, UDP:$UDP_PORT, HTTP:$HTTP_PORT"
sleep 2

# 3. Inject 4 SRT Streams (Caller mode to Listener)
echo -e "TSP: Injecting 4 SRT streams..."
for i in {1..4}; do
    ./build/tsp -f "$SAMPLE_FILE" -b 2000000 --srt-url "srt://127.0.0.1:$SRT_PORT?mode=caller&streamid=SRT-ST-$i" -l > tsp_srt_$i.log 2>&1 &
done

# 4. Inject 1 UDP Stream
echo -e "TSP: Injecting 1 UDP stream on port $UDP_PORT..."
./build/tsp -i 127.0.0.1 -p $UDP_PORT -f "$SAMPLE_FILE" -b 5000000 -l > tsp_udp.log 2>&1 &

echo "WAIT: Collecting metrics for 10 seconds..."
sleep 10

# 5. Verify Metrics
echo -e "${BLUE}--- Metrics Verification ---${NC}"
METRICS=$(curl -s http://localhost:$HTTP_PORT/metrics)

# Check for SRT streams in metrics (using stream_id)
SRT_COUNT=$(echo "$METRICS" | grep "tsa_metrology_physical_bitrate_bps" | grep -c "stream_id=\"SRT-")
UDP_FOUND=$(echo "$METRICS" | grep "tsa_metrology_physical_bitrate_bps" | grep -c "stream_id=\"UDP-19001\"")

echo -e "Found $SRT_COUNT SRT streams in metrics"
echo -e "UDP Stream Found: $UDP_FOUND"

# Check for Professional SRT stats (from bstats)
RTT_FOUND=$(echo "$METRICS" | grep -c "tsa_srt_rtt_ms")

if [ "$SRT_COUNT" -ge 4 ] && [ "$UDP_FOUND" -ge 1 ]; then
    # Even if RTT is 0, we want to see bitrates
    SRT_BITRATE_TOTAL=$(echo "$METRICS" | grep "tsa_metrology_physical_bitrate_bps" | grep "stream_id=\"SRT-" | awk '{sum+=$2} END {print sum}')
    echo -e "Total SRT Bitrate: $SRT_BITRATE_TOTAL bps"
    
    if (( $(echo "$SRT_BITRATE_TOTAL > 0" | bc -l) )); then
        echo -e "RESULT: ${GREEN}SUCCESS${NC}"
        killall tsa_server_pro tsp
        exit 0
    else
        echo -e "RESULT: ${RED}FAILED${NC} (SRT bitrates are 0)"
        echo "Server Log Tail:"
        tail -n 20 server_pro.log
        killall tsa_server_pro tsp
        exit 1
    fi
else
    echo -e "RESULT: ${RED}FAILED${NC} (Streams not found)"
    echo "Metrics summary:"
    echo "$METRICS" | grep "tsa_metrology_physical_bitrate_bps"
    killall tsa_server_pro tsp
    exit 1
fi
