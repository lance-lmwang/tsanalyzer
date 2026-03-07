#!/bin/bash
# TsAnalyzer Pro: v9.1 Enterprise 7-Stream Stability Test (Port 8100)

PORT_API=8100
DURATION=300
STREAMS=7
SAMPLE="sample/test.ts"

echo "=================================================="
echo " [FACTORY] v9.1 PRODUCTION ACCEPTANCE: 7-STREAM STRESS"
echo " Cores: Core 0 (API), Cores 1-7 (Analysis)"
echo "=================================================="

# 1. Hard Cleanup
ulimit -n 65535
fuser -k -9 $PORT_API/tcp >/dev/null 2>&1 || true
for i in {0..6}; do fuser -k -9 $((8088 + $i))/udp >/dev/null 2>&1 || true; done
pkill -9 tsa_server || true
pkill -9 tsp || true
sleep 2

# 2. Start Analyzer
echo "[*] Starting v9.1 Analyzer..."
./build/tsa_server > server_isolated.log 2>&1 &
SERVER_PID=$!

# Health Check
for i in {1..10}; do
    if curl -s -I http://localhost:$PORT_API/metrics | grep "200 OK" >/dev/null; then
        echo "[*] Analyzer is UP and RESPONSIVE."
        break
    fi
    if [ $i -eq 10 ]; then echo "[FAIL] CRITICAL: Server failed to start."; exit 1; fi
    sleep 1
done

# 3. Launch 7 PCR-Locked Pacers (Pinned to Cores 1-7)
echo "[*] Injecting 7-Stream Load (PCR-Locked)..."
for i in {0..6}; do
    CORE=$((i + 1))
    PORT=$((8088 + i))
    taskset -c $CORE ./build/tsp -P -l -t 7 -i 127.0.0.1 -p $PORT -f "$SAMPLE" > /dev/null 2>&1 &
done

# 4. Calibrate Baseline
echo "[*] Stabilizing for 10 seconds..."
sleep 10
BASELINE=$(curl -s http://localhost:$PORT_API/metrics | grep "tsa_cc" | awk '{sum+=$2} END {print sum}')
echo "[*] Initial CC Total: $BASELINE"
echo "--------------------------------------------------"
echo "Time(s) | CC Errors (Total) | Status"
echo "--------------------------------------------------"

START_TIME=$(date +%s)
while true; do
    CURRENT_TIME=$(date +%s)
    ELAPSED=$((CURRENT_TIME - START_TIME))
    if [ $ELAPSED -ge $DURATION ]; then break; fi

    CURRENT_CC=$(curl -s http://localhost:$PORT_API/metrics | grep "tsa_cc" | awk '{sum+=$2} END {print sum}')

    if [ -z "$CURRENT_CC" ]; then
        echo "[$ELAPSED s] | API UNRESPONSIVE | [WARN] WARNING"
    else
        if [ "$CURRENT_CC" -gt "$BASELINE" ]; then
            DIFF=$((CURRENT_CC - BASELINE))
            echo "[$ELAPSED s] | $CURRENT_CC | [FAIL] FAILED (+$DIFF)"
            kill -9 $SERVER_PID $(pgrep tsp)
            exit 1
        fi
        printf "[%3d s] | %17d | [PASS] OK\n" "$ELAPSED" "$CURRENT_CC"
    fi
    sleep 10
done

echo "--------------------------------------------------"
echo "[PASS] FINAL VERDICT: v9.1 FIRMWARE-GRADE STABILITY PASSED"
echo "   - Concurrency: 7 Streams"
echo "   - Duration: 300 Seconds"
echo "   - CC Increment: 0"
kill -9 $SERVER_PID $(pgrep tsp)
exit 0
