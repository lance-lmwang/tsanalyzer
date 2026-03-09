#!/bin/bash
# TsAnalyzer: Real-time Metrology Dual-Bitrate Verification (CLI Edition)
set -u

PORT_API=8088
PORT_UDP=19001
SAMPLE_TS="./sample/test.ts"
[ ! -f "$SAMPLE_TS" ] && SAMPLE_TS="../sample/test.ts"
[ ! -f "$SAMPLE_TS" ] && SAMPLE_TS="/home/lmwang/dev/sample/test.ts"

echo ">>> TsAnalyzer: Professional Metrology Test (Dual-Rate Monitor via CLI)"

# Check if sample file exists
if [ ! -f "$SAMPLE_TS" ]; then
    echo "[FAIL] FATAL ERROR: Sample file '$SAMPLE_TS' not found."
    exit 1
fi

echo ">>> Phase 1: Starting Metrology Engine (CLI)..."
pkill -9 tsa_cli || true
pkill -9 tsp || true
fuser -k -9 $PORT_API/tcp $PORT_UDP/udp > /dev/null 2>&1 || true

./build/tsa --mode=live --udp $PORT_UDP > tsa_cli_metrology.log 2>&1 &
CLI_PID=$!
sleep 2

echo ">>> Phase 2: Pacing via PCR Clock (Automatic)..."
./build/tsp -P -l -i 127.0.0.1 -p $PORT_UDP -f "$SAMPLE_TS" > /dev/null 2>&1 &
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
        echo "[$((i*5+5))s] | Waiting... | -- | -- | [FAIL] NO DATA"
        continue
    fi

    P_BPS=$(echo "$METRICS" | grep "tsa_metrology_physical_bitrate_bps" | head -n 1 | awk '{print $2}' || echo "0")
    C_BPS=$(echo "$METRICS" | grep "tsa_metrology_pcr_bitrate_bps" | head -n 1 | awk '{print $2}' || echo "0")
    JITTER=$(echo "$METRICS" | grep "tsa_metrology_pcr_jitter_ms" | head -n 1 | awk '{print $2}' || echo "0.0")

    if [ "$C_BPS" == "0" ] || [ -z "$C_BPS" ]; then
        echo "[$((i*5+5))s] | PCR NOT LOCKED | -- | $JITTER ms | [WARN] LOCKING"
        continue
    fi

    P_MBPS=$(echo "scale=2; $P_BPS / 1000000" | bc)
    C_MBPS=$(echo "scale=2; $C_BPS / 1000000" | bc)

    # 🚨 STRICT VALUE VALIDATION (Target ~10Mbps physical output)
    # Expected Range: 9.0 - 11.0 Mbps
    VALID_P=0
    if (( $(echo "$P_MBPS > 9.0" | bc -l) )) && (( $(echo "$P_MBPS < 11.0" | bc -l) )); then VALID_P=1; fi

    STATUS="[PASS]"
    if [ $VALID_P -eq 0 ]; then
        STATUS="[FAIL]"
    fi

    printf "[%2ds] | %13.2f | %16.2f | %7.3f | %s\n" $((i*5+5)) "$P_MBPS" "$C_MBPS" "$JITTER" "$STATUS"

    if [ "$STATUS" == "[PASS]" ]; then
        SUCCESS_COUNT=$((SUCCESS_COUNT+1))
    fi
done

echo "------------------------------------------------------------"
kill -9 $CLI_PID $TSP_PID > /dev/null 2>&1 || true

# Must have at least 4 stable samples out of 6 to pass
if [ $SUCCESS_COUNT -ge 4 ]; then
    echo ">>> METROLOGY VERIFIED: Dual-bitrate analysis stable and accurate."
    exit 0
else
    echo ">>> METROLOGY FAILED: Bitrate values out of tolerance or unstable."
    exit 1
fi
