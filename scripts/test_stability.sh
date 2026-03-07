#!/bin/bash

# Configuration
TS_FILE="./sample/cctvhd.ts"
[ ! -f "$TS_FILE" ] && TS_FILE="../sample/cctvhd.ts"
[ ! -f "$TS_FILE" ] && TS_FILE="/home/lmwang/dev/sample/cctvhd.ts"
BIN="./build/tsp"
LOG_FILE="stability_test.log"
DURATION=300 # 5 minutes

echo "=== TsPacer Long-Term Stability & Loop Seamlessness Test ==="
echo "Target: 300 seconds (~10 loops of cctvhd.ts)"

if [ ! -f "$BIN" ]; then
    echo "Error: tsp binary not found."
    exit 1
fi

# 1. Setup Environment
setcap cap_sys_nice,cap_ipc_lock,cap_net_raw=ep "$BIN" 2>/dev/null || true

# 2. Run TsPacer
echo "2. Running TsPacer (PCR Mode + Loop)..."
$BIN -P -l -i 127.0.0.1 -p 1234 -c 0 -f $TS_FILE > $LOG_FILE 2>&1 &
TSPACER_PID=$!

# 3. Monitor
for ((i=1; i<=$DURATION; i++)); do
    echo -ne "Progress: $i/$DURATION s\r"
    sleep 1
done
echo -e "\nMonitoring complete. Stopping TsPacer..."

kill $TSPACER_PID
wait $TSPACER_PID 2>/dev/null

# 4. Data Analysis
echo "4. Analyzing Stability Data..."

# Extract PCR Rates, excluding the initial 0.00 before sync
RATES=$(grep "PCR Rate:" $LOG_FILE | awk '{print $12}' | grep -E '^[0-9.]+$' | grep -v "^0.00$")

if [ -z "$RATES" ]; then
    echo "Error: No valid data captured in log."
    exit 1
fi

MAX_RATE=$(echo "$RATES" | sort -nr | head -n 1)
MIN_RATE=$(echo "$RATES" | sort -n | head -n 1)
AVG_RATE=$(echo "$RATES" | awk '{sum+=$1; count++} END {print sum/count}')
DROPS=$(grep "Drops:" $LOG_FILE | tail -n 1 | awk '{print $22}')

JITTER_DIFF=$(echo "$MAX_RATE - $MIN_RATE" | bc)

echo "-------------------------------------------"
echo "  Total Run Time:   $DURATION seconds"
echo "  Average Bitrate:  $AVG_RATE Mbps"
echo "  Maximum Bitrate:  $MAX_RATE Mbps"
echo "  Minimum Bitrate:  $MIN_RATE Mbps"
echo "  Bitrate Swing:    $JITTER_DIFF Mbps"
echo "  Packet Drops:     $DROPS"
echo "-------------------------------------------"

# Threshold check: 0.5 Mbps swing is acceptable for PCR-mode on a PC
if (( $(echo "$JITTER_DIFF < 0.5" | bc -l) )); then
    echo "  [PASS] Stability verified. Bitrate swing within tolerance."
    exit 0
else
    echo "  [FAIL] Stability issue detected."
    exit 1
fi
