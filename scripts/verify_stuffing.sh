#!/bin/bash

# Configuration
TS_FILE="./sample/cctvhd.ts"
[ ! -f "$TS_FILE" ] && TS_FILE="../sample/cctvhd.ts"
[ ! -f "$TS_FILE" ] && TS_FILE="/home/lmwang/dev/sample/cctvhd.ts"
BIN="./build/tsp"
LOG_FILE="verify_stuffing.log"
TARGET_MBPS=9
DURATION=30

# Force C locale
export LC_ALL=C

echo "=== TsPacer CBR Stuffing (8M -> 9M) Verification ==="

if [ ! -f "$BIN" ]; then
    echo "Error: tsp binary not found. Please build it first."
    exit 1
fi

if [ ! -f "$TS_FILE" ]; then
    echo "Error: Test file $TS_FILE not found."
    exit 1
fi

# 1. Request RT permissions
setcap cap_sys_nice,cap_ipc_lock,cap_net_raw=ep "$BIN" 2>/dev/null || true

# 2. Run TsPacer: Filling 8M PCR stream to 9M CBR
echo "2. Running TsPacer: Filling 8M PCR stream to 9M CBR..."
timeout ${DURATION}s $BIN -P -b $((TARGET_MBPS * 1000000)) -i 127.0.0.1 -p 1234 -c 0 -f $TS_FILE > $LOG_FILE 2>&1

# 3. Analyzing Results
echo "3. Analyzing Results..."

LAST_LINE=$(grep "Cur Rate:" $LOG_FILE | tail -n 1)

if [ -z "$LAST_LINE" ]; then
    echo "Error: No internal bitrate statistics found in log."
    cat $LOG_FILE
    exit 1
fi

# Extract values
INTERNAL_RATE=$(echo "$LAST_LINE" | sed -E 's/.*Cur Rate: ([0-9.]+) Mbps.*/\1/')
PCR_RATE=$(echo "$LAST_LINE" | sed -E 's/.*PCR Rate: ([0-9.]+) Mbps.*/\1/')
TOTAL_PKTS=$(echo "$LAST_LINE" | sed -E 's/.*UDP Pkts: ([0-9]+),.*/\1/')

echo "------------------------------------------------"
echo "  Target Output Bitrate:  $TARGET_MBPS.00 Mbps"
echo "  Actual Output Bitrate:  $INTERNAL_RATE Mbps"
echo "  Original PCR Bitrate:   $PCR_RATE Mbps"
echo "  Total UDP Packets:      $TOTAL_PKTS"
echo "------------------------------------------------"

# Calculate error relative to Target
PERCENT_ERROR=$(echo "scale=4; a=($INTERNAL_RATE - $TARGET_MBPS); if(a<0)a=-a; a*100/$TARGET_MBPS" | bc -l)
DISPLAY_ERROR=$(echo "scale=2; $PERCENT_ERROR / 1" | bc -l)

echo "  Output Precision Error: $DISPLAY_ERROR%"

if (( $(echo "$PERCENT_ERROR < 0.5" | bc -l) )); then
    echo "  [PASS] CBR Stuffing is accurate (within 0.5% tolerance)."
    exit 0
else
    echo "  [FAIL] Output bitrate deviation too high ($DISPLAY_ERROR%)."
    exit 1
fi
