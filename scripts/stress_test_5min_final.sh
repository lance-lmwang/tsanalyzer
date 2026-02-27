#!/bin/bash
# TsAnalyzer Pro: 5-Minute Industrial Stress Test (8 Streams / 64 Mbps)

PORT_API=8099
DURATION=300
STREAMS=8
SAMPLE="/home/lmwang/dev/sample/cctvhd.ts"

echo "=================================================="
echo " 🏭 PRODUCTION ACCEPTANCE: 5-Min Extreme Stress"
echo "=================================================="

# 1. Cleanup
fuser -k -9 $PORT_API/tcp >/dev/null 2>&1 || true
for i in {0..7}; do fuser -k -9 $((12345 + $i))/udp >/dev/null 2>&1 || true; done
pkill -9 tsa_server || true
pkill -9 tsp || true
sleep 2

# 2. Launch Analyzer
./build/tsa_server > /dev/null 2>&1 &
SERVER_PID=$!
sleep 2

# 3. Launch 8 Pacers
echo "[*] Launching 8 PCR-Locked Pacers..."
for i in {0..7}; do
    ./build/tsp -P -l -t 7 -i 127.0.0.1 -p $((12345 + $i)) -f "$SAMPLE" > /dev/null 2>&1 &
done

# 4. Calibrate Baseline
echo "[*] Stabilizing for 10 seconds..."
sleep 10
BASELINE=$(curl -s http://localhost:$PORT_API/metrics | grep "tsa_cc" | awk '{sum+=$2} END {print sum}')
echo "[*] Baseline CC Total: $BASELINE"
echo "--------------------------------------------------"
echo "Time(s) | Total CC Errors | Status"
echo "--------------------------------------------------"

START_TIME=$(date +%s)
while true; do
    CURRENT_TIME=$(date +%s)
    ELAPSED=$((CURRENT_TIME - START_TIME))
    if [ $ELAPSED -ge $DURATION ]; then break; fi

    CURRENT_CC=$(curl -s http://localhost:$PORT_API/metrics | grep "tsa_cc" | awk '{sum+=$2} END {print sum}' || echo "$BASELINE")
    
    STATUS="STABLE"
    if [ "$CURRENT_CC" -gt "$BASELINE" ]; then
        DIFF=$((CURRENT_CC - BASELINE))
        echo "[$ELAPSED s] | $CURRENT_CC | ❌ FAILED (+$DIFF errors)"
        kill $SERVER_PID $(pgrep tsp)
        exit 1
    fi

    printf "[%3d s] | %15d | ✅ OK
" "$ELAPSED" "$CURRENT_CC"
    sleep 10
done

echo "--------------------------------------------------"
echo "✅ FINAL VERDICT: 5-MINUTE PRODUCTION STABILITY PASSED"
echo "   - Streams: $STREAMS"
echo "   - CC Increment: 0"
kill $SERVER_PID $(pgrep tsp)
exit 0
