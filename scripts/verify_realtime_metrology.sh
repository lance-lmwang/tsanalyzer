#!/bin/bash
# TsAnalyzer: Real-time Metrology Dual-Bitrate Verification (CLI Edition)
set -u

PORT_API=12345
PORT_UDP=19001
SAMPLE_TS="./sample/test_1m.ts"
if [ ! -f "$SAMPLE_TS" ]; then SAMPLE_TS="./sample/btvhd.ts"; fi
if [ ! -f "$SAMPLE_TS" ]; then SAMPLE_TS="../sample/test_1m.ts"; fi

echo ">>> TsAnalyzer: Professional Metrology Test (Dual-Rate Monitor via CLI)"

# Check if sample file exists
if [ ! -f "$SAMPLE_TS" ]; then
    echo "❌ FATAL ERROR: Sample file '$SAMPLE_TS' not found."
    exit 1
fi

echo ">>> Phase 1: Starting Metrology Engine (CLI)..."
pkill -9 tsa_cli || true
pkill -9 tsp || true
fuser -k -9 $PORT_API/tcp $PORT_UDP/udp > /dev/null 2>&1 || true

./build/tsa_cli --mode=live --udp $PORT_UDP > tsa_cli_metrology.log 2>&1 &
CLI_PID=$!
sleep 2

echo ">>> Phase 2: Pacing via PCR Clock (Automatic)..."
./build/tsp -P -l -m 0x0202 -i 127.0.0.1 -p $PORT_UDP -f "$SAMPLE_TS" > /dev/null 2>&1 &
TSP_PID=$!

echo ">>> Phase 3: Monitoring Stability (30s)..."
echo "Time | Physical Mbps | PCR-Content Mbps | Jitter | Status"
echo "------------------------------------------------------------"

sleep 5

SUCCESS_COUNT=0
for i in {1..6}; do
    sleep 5
    METRICS=$(curl -s http://localhost:$PORT_API/metrics || echo "")

    if [ -z "$METRICS" ]; then
        echo "[$((i*5+5))s] | Waiting... | -- | -- | ❌ NO DATA"
        continue
    fi

    P_BPS=$(echo "$METRICS" | grep "^tsa_physical_bitrate_bps" | head -n 1 | awk '{print $2}' || echo "0")
    C_BPS=$(echo "$METRICS" | grep "^tsa_pcr_bitrate_bps" | head -n 1 | awk '{print $2}' || echo "0")
    JITTER=$(echo "$METRICS" | grep "^tsa_pcr_jitter_ms" | head -n 1 | awk '{print $2}' || echo "0.0")

    if [ "$C_BPS" == "0" ] || [ -z "$C_BPS" ]; then
        echo "[$((i*5+5))s] | PCR NOT LOCKED | -- | $JITTER ms | ⚠️ LOCKING"
        continue
    fi

    P_MBPS=$(echo "scale=2; $P_BPS / 1000000" | bc)
    C_MBPS=$(echo "scale=2; $C_BPS / 1000000" | bc)

    J_STATUS="✅"
    if (( $(echo "$JITTER > 10.0" | bc -l) )); then J_STATUS="⚠️"; fi

    printf "[%2ds] | %13.2f | %16.2f | %7.3f | %s OK\n" $((i*5+5)) "$P_MBPS" "$C_MBPS" "$JITTER" "$J_STATUS"
    SUCCESS_COUNT=$((SUCCESS_COUNT+1))
done

echo "------------------------------------------------------------"
kill -9 $CLI_PID $TSP_PID > /dev/null 2>&1 || true

if [ $SUCCESS_COUNT -gt 0 ]; then
    echo ">>> METROLOGY VERIFIED: Dual-bitrate analysis stable."
    exit 0
else
    echo ">>> METROLOGY FAILED: Could not receive valid metrics."
    exit 1
fi
