#!/bin/bash

# Configuration
TS_FILE=$1
if [ -z "$TS_FILE" ]; then
    TS_FILE="./sample/cctvhd.ts"
    [ ! -f "$TS_FILE" ] && TS_FILE="../../sample/cctvhd.ts"
    [ ! -f "$TS_FILE" ] && TS_FILE="/home/lmwang/dev/sample/cctvhd.ts"
fi

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

# 1. Request RT permissions (optional)
echo "1. Attempting to set RT permissions..."
# Ensure setcap doesn't fail the script and output is suppressed
setcap cap_sys_nice,cap_ipc_lock,cap_net_raw=ep "$BIN" 2>/dev/null || true

# 2. Run tsp in PCR Mode (-P)
echo "2. Running tsp in PCR mode for $DURATION seconds..."
timeout ${DURATION}s $BIN -P -i 127.0.0.1 -p 1234 -c 0 -f "$TS_FILE" > "$LOG_FILE" 2>&1

# 3. Analyze Results
echo "3. Analyzing results..."

# Extract the last line that has a non-zero Cur Rate
LAST_LINE=$(grep "Cur Rate:" "$LOG_FILE" | grep -v "Cur Rate: 0.00 Mbps" | tail -n 1)

if [ -z "$LAST_LINE" ]; then
    echo "Error: No valid internal bitrate statistics found in log."
    # Fallback to any line if we can't find non-zero
    LAST_LINE=$(grep "Cur Rate:" "$LOG_FILE" | tail -n 1)
fi

if [ -z "$LAST_LINE" ]; then
    echo "Error: No statistics found at all."
    exit 1
fi

# Extract values using robust regex matching the actual output:
# Pkts: 516929, PPS: 6761, Cur Rate: 71.18 Mbps, PCR Rate: 12.92 Mbps, ...
INTERNAL_RATE=$(echo "$LAST_LINE" | sed -E 's/.*Cur Rate: ([0-9.]+) Mbps.*/\1/')
PCR_RATE=$(echo "$LAST_LINE" | sed -E 's/.*PCR Rate: ([0-9.]+) Mbps.*/\1/')
TOTAL_PKTS=$(echo "$LAST_LINE" | sed -E 's/.*Pkts: ([0-9]+),.*/\1/')

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

if (( $(echo "$PERCENT_ERROR < 1.0" | bc -l) )); then
    echo "   [PASS] PCR-Locked pacing is accurate (within 1.0% tolerance)."
    exit 0
else
    echo "   [FAIL] PCR pacing deviation too high ($DISPLAY_ERROR%)."
    exit 1
fi
