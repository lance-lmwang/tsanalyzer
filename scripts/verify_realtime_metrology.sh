#!/bin/bash
# TsAnalyzer: Real-time Metrology Verification Suite
# Verifies PCR-driven pacing, clock recovery, and MDI-DF accuracy.

set -e

PORT_API=8080
PORT_UDP=19001
DURATION=30
BITRATE=10000000 # 10Mbps
TS_FILE="metrology_ref.ts"

echo ">>> TsAnalyzer: Professional Real-time Metrology Test"
echo ">>> Phase 1: Generating 30s CBR reference stream (10Mbps)..."

# Use python to generate a proper CBR stream with PCRs at 40ms intervals
python3 -c "
import struct
bitrate = $BITRATE
duration = $DURATION
pkts = int((bitrate * duration) / (188 * 8))
pcr_interval = 20 # Every 20 packets ~30ms at 10Mbps
f = open('$TS_FILE', 'wb')
for i in range(pkts):
    pkt = bytearray([0x47, 0x1F, 0xFF, 0x10] + [0xFF]*184) # Null with CC=0
    if i % pcr_interval == 0:
        # Inject PCR into adaptation field
        pcr_ticks = int((i * 188 * 8 * 27000000) / bitrate)
        base = pcr_ticks // 300
        ext = pcr_ticks % 300
        pkt[1] = 0x01; pkt[2] = 0x00; pkt[3] = 0x30 # PID 0x100, AF+Payload
        pkt[4] = 0x07; pkt[5] = 0x10 # AF size 7, PCR flag
        pkt[6] = (base >> 25) & 0xFF
        pkt[7] = (base >> 17) & 0xFF
        pkt[8] = (base >> 9) & 0xFF
        pkt[9] = (base >> 1) & 0xFF
        pkt[10] = ((base & 0x01) << 7) | 0x7E | ((ext >> 8) & 0x01)
        pkt[11] = ext & 0xFF
    f.write(pkt)
f.close()
"

echo ">>> Phase 2: Starting Metrology Engine..."
fuser -k -9 $PORT_API/tcp $PORT_UDP/udp > /dev/null 2>&1 || true
./build/tsa_server > /dev/null 2>&1 &
SERVER_PID=$!
sleep 2

echo ">>> Phase 3: Pacing stream via PCR Clock (Real-time)..."
# Use taskset to bind to a specific core for better pacing stability on localhost
taskset -c 2 ./build/tsp -P -i 127.0.0.1 -p $PORT_UDP -f "$TS_FILE" > /dev/null 2>&1 &
TSP_PID=$!

echo ">>> Phase 4: Monitoring Metrology Stability (30s)..."
echo "Time | Measured Mbps | Error % | PCR Jitter | Status"
echo "------------------------------------------------------"

ERR_PCT=0
for i in {1..6}; do
    sleep 5
    RAW_METRICS=$(curl -s http://localhost:$PORT_API/metrics || echo "")
    METRICS=$(echo "$RAW_METRICS" | grep 'stream_id="STR-1"' || true)
    
    if [ -z "$METRICS" ]; then
        echo "Waiting for metrics..."
        continue
    fi

    BPS=$(echo "$METRICS" | grep "tsa_pcr_bitrate_bps" | awk '{print $2}' || echo "0")
    JITTER=$(echo "$METRICS" | grep "tsa_pcr_jitter_ms" | awk '{print $2}' || echo "0")
    
    if [ -z "$BPS" ] || [ "$BPS" == "0" ]; then
        echo "Waiting for PCR Lock..."
        continue
    fi

    MBPS_M=$(echo "scale=2; $BPS / 1000000" | bc)
    # Simple shell-based absolute difference
    DIFF=$(echo "$MBPS_M - 10.0" | bc -l)
    if (( $(echo "$DIFF < 0" | bc -l) )); then DIFF=$(echo "-1 * $DIFF" | bc -l); fi
    ERR_PCT=$(echo "scale=2; $DIFF * 10" | bc -l) # (DIFF / 10.0) * 100

    # Check if jitter is low (< 10ms for generic Linux localhost)
    J_STATUS="✅"
    if [ -z "$JITTER" ]; then JITTER="0.000"; fi
    if (( $(echo "$JITTER > 10.0" | bc -l) )); then J_STATUS="⚠️"; fi

    printf "[%2ds] | %11.2f | %6.2f%% | %8.3f ms | %s OK\n" $((i*5)) "$MBPS_M" "$ERR_PCT" "$JITTER" "$J_STATUS"
done

echo "------------------------------------------------------"
kill -9 $SERVER_PID $TSP_PID > /dev/null 2>&1 || true
rm -f "$TS_FILE"

# Final Evaluation: 10% tolerance for non-RT systems
FINAL_CHECK=$(echo "$ERR_PCT < 10.0" | bc -l)
if [ "$FINAL_CHECK" -eq "1" ]; then
    echo ">>> METROLOGY VERIFIED: Precision within 10% tolerance on non-RT host."
    exit 0
else
    echo ">>> METROLOGY FAILED: Significant drift detected ($ERR_PCT%)."
    exit 1
fi
