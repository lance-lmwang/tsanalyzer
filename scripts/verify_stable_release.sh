#!/bin/bash
# TsAnalyzer: Stable Release & Real-time Stress Test
set -u

PORT_API=8088
SAMPLE_TS="./sample/test.ts"
[ ! -f "$SAMPLE_TS" ] && SAMPLE_TS="../sample/test.ts"
[ ! -f "$SAMPLE_TS" ] && SAMPLE_TS="/home/lmwang/dev/sample/test.ts"
STREAMS=4

echo ">>> TsAnalyzer PRO: Stable Release Validation"
echo ">>> Scenario: $STREAMS Concurrent Real-time Streams (PCR-Locked)"

# 1. Start Server
fuser -k -9 $PORT_API/tcp 19001-19008/udp > /dev/null 2>&1 || true
./build/tsa_server > /dev/null 2>&1 &
SERVER_PID=$!
sleep 3

# 2. Launch Multiple Pacers
echo ">>> Launching $STREAMS Pacing Tasks..."
for i in $(seq 1 $STREAMS); do
    UDP_PORT=$((19000 + i))
    CORE=$((i + 1)) # Bind to cores 2, 3, 4, 5...
    taskset -c $CORE ./build/tsp -P -l -m 0x0202 -i 127.0.0.1 -p $UDP_PORT -f "$SAMPLE_TS" > /dev/null 2>&1 &
    eval "TSP_PID_$i=\$!"
done

echo ">>> Stabilizing for 10s..."
sleep 10

echo "STR | PCR Mbps | Jitter (ms) | Latency (ns) | Status"
echo "------------------------------------------------------"

TOTAL_ERR=0
for i in {1..4}; do
    sleep 5
    METRICS=$(curl -s http://localhost:$PORT_API/metrics)

    for s in $(seq 1 $STREAMS); do
        SID="STR-$s"
        # Extract specific metric for this stream
        BPS=$(echo "$METRICS" | grep "tsa_metrology_pcr_bitrate_bps{stream_id=\"$SID\"}" | awk '{print $2}')
        JIT=$(echo "$METRICS" | grep "tsa_metrology_pcr_jitter_ms{stream_id=\"$SID\"}" | awk '{print $2}')
        LAT=$(echo "$METRICS" | grep "tsa_engine_processing_latency_ns{stream_id=\"$SID\"}" | awk '{print $2}')

        if [ -z "$BPS" ]; then BPS=0; fi
        if [ -z "$JIT" ]; then JIT=0.0; fi
        if [ -z "$LAT" ]; then LAT=0; fi

        MBPS=$(echo "scale=2; $BPS / 1000000" | bc)

        # Target 8.00 Mbps check
        DIFF=$(echo "$MBPS - 8.00" | bc -l)
        # abs diff
        IS_NEG=$(echo "$DIFF < 0" | bc -l)
        if [ "$IS_NEG" -eq "1" ]; then DIFF=$(echo "-1 * $DIFF" | bc -l); fi

        STATUS="[PASS]"
        # If MBPS is 0, it means not locked yet
        if [ "$BPS" -eq "0" ]; then STATUS="[WAIT]"; fi
        if (( $(echo "$DIFF > 0.5" | bc -l) )) && [ "$BPS" -ne "0" ]; then STATUS="[WARN]"; fi

        printf "%s | %8.2f | %11.3f | %12d | %s\n" "$SID" "$MBPS" "$JIT" "$LAT" "$STATUS"
    done
    echo "------------------------------------------------------"
done

# Cleanup
killall tsa_server tsp > /dev/null 2>&1 || true
echo ">>> STABILITY TEST COMPLETE."
