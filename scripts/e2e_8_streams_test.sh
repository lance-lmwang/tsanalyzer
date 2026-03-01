#!/bin/bash
# TsAnalyzer Pro - E2E 8-Stream High-Perf Stability Test

BASE_UDP_PORT=19001
SAMPLE_FILE="/home/lmwang/sample/cctvhd.ts"
TEST_DURATION=60 # 1 minute

if [ ! -f "$SAMPLE_FILE" ]; then
    echo "Error: Sample file $SAMPLE_FILE not found."
    # Try to find any TS file in samples
    SAMPLE_FILE=$(find /home/lmwang/sample -name "*.ts" | head -n 1)
    if [ -z "$SAMPLE_FILE" ]; then
        echo "No TS file found. Creating dummy TS file..."
        SAMPLE_FILE="/tmp/dummy.ts"
        dd if=/dev/zero bs=188 count=10000 > "$SAMPLE_FILE"
        # Put sync bytes
        for i in {0..9999}; do
            printf '\x47' | dd of="$SAMPLE_FILE" bs=1 seek=$((i*188)) conv=notrunc 2>/dev/null
        done
    fi
fi

echo "Using sample file: $SAMPLE_FILE"

function cleanup() {
    echo "Stopping simulation..."
    pkill -9 tsa_server
    pkill -9 tsp
    sleep 1
}
trap cleanup EXIT

echo "=== [1/2] STARTING HIGH-PERF SERVER ==="
./build/tsa_server > e2e_8_streams.log 2>&1 &
SERVER_PID=$!
sleep 2

echo "=== [2/2] INJECTING 8 STREAMS AT 10MBPS EACH ==="
for i in {1..8}; do
    UDP_PORT=$((BASE_UDP_PORT + i - 1))
    BR=10000000 # 10 Mbps

    echo "  -> Stream $i on port $UDP_PORT (10 Mbps)"
    ./build/tsp -i 127.0.0.1 -p $UDP_PORT -l -f "$SAMPLE_FILE" -b $BR > /dev/null 2>&1 &
done

echo "===================================================="
echo "STABILITY MONITORING ACTIVE (Duration: ${TEST_DURATION}s)"
echo "===================================================="

# Monitor the output of tsa_server
tail -f e2e_8_streams.log &
TAIL_PID=$!

sleep $TEST_DURATION

kill $TAIL_PID
echo "===================================================="
echo "TEST COMPLETE. CHECKING FINAL STATUS..."
echo "===================================================="
grep "STR-" e2e_8_streams.log | tail -n 8
