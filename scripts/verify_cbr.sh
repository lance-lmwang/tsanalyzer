#!/bin/bash

# Configuration
TS_FILE="/home/lmwang/dev/cctvhd.ts"
BIN="./build/tsp"
LOG_FILE="verify_pcr.log"
DURATION=60

# Force C locale for consistent decimal point and numeric formatting
export LC_ALL=C

echo "=== TsPacer PCR-Locked (PCR-Synchronous) Verification ==="

if [ ! -f "$BIN" ]; then
    echo "Error: tsp binary not found. Please build it first."
    exit 1
fi

if [ ! -f "$TS_FILE" ]; then
    echo "Error: Test file $TS_FILE not found."
    exit 1
fi

# 1. Request RT permissions
echo "1. Setting RT permissions..."
setcap cap_sys_nice,cap_ipc_lock,cap_net_raw=ep "$BIN"

# 2. Run tsp in PCR Mode (-P)
echo "2. Running tsp in PCR mode for $DURATION seconds..."
timeout ${DURATION}s $BIN -P -i 127.0.0.1 -p 1234 -c 0 -f $TS_FILE > $LOG_FILE 2>&1

# 3. Analyze Results
echo "3. Analyzing results..."

# Extract last valid stats line
LAST_LINE=$(grep "Cur Rate:" $LOG_FILE | tail -n 1)

if [ -z "$LAST_LINE" ]; then
    echo "Error: No internal bitrate statistics found in log."
    cat $LOG_FILE
    exit 1
fi

# Extract values using robust regex
INTERNAL_RATE=$(echo "$LAST_LINE" | sed -E 's/.*Cur Rate: ([0-9.]+) Mbps.*/\1/')
PCR_RATE=$(echo "$LAST_LINE" | sed -E 's/.*PCR Rate: ([0-9.]+) Mbps.*/\1/')
TOTAL_PKTS=$(echo "$LAST_LINE" | sed -E 's/.*UDP Pkts: ([0-9]+),.*/\1/')

if [ -z "$INTERNAL_RATE" ] || [ -z "$PCR_RATE" ]; then
    echo "Error: Failed to parse bitrate from log line."
    echo "Line: $LAST_LINE"
    exit 1
fi

echo "   Detected Stream Bitrate (PCR): $PCR_RATE Mbps"
echo "   Actual Transmission Bitrate:   $INTERNAL_RATE Mbps"
echo "   Total Packets Sent:            $TOTAL_PKTS"

# Calculate percentage error relative to source PCR rate
PERCENT_ERROR=$(echo "scale=4; a=($INTERNAL_RATE - $PCR_RATE); if(a<0)a=-a; a*100/$PCR_RATE" | bc -l)
DISPLAY_ERROR=$(echo "scale=2; $PERCENT_ERROR / 1" | bc -l)

echo "   Precision Error: $DISPLAY_ERROR%"

if (( $(echo "$PERCENT_ERROR < 0.5" | bc -l) )); then
    echo "   [PASS] PCR-Locked pacing is ultra-accurate (within 0.5% tolerance)."
    exit 0
else
    echo "   [FAIL] PCR pacing deviation too high ($DISPLAY_ERROR%)."
    exit 1
fi
