#!/bin/bash

# Configuration
TS_FILE="./sample/cctvhd.ts"
[ ! -f "$TS_FILE" ] && TS_FILE="../sample/cctvhd.ts"
[ ! -f "$TS_FILE" ] && TS_FILE="/home/lmwang/dev/sample/cctvhd.ts"
CORE=0
BITRATE=20000000
BIN="./build/tsp"

echo "=== TsPacer RT Hardening Automated Verification ==="

if [ ! -f "$BIN" ]; then
    echo "Error: tsp binary not found in build directory. Please build it first."
    exit 1
fi

# 1. Start tsp in background
echo "1. Starting tsp on core $CORE with $TS_FILE..."
$BIN -b $BITRATE -i 127.0.0.1 -p 1234 -c $CORE -f $TS_FILE > tsp_output.log 2>&1 &
TSP_PID=$!

# Wait a bit for thread to initialize and mlockall to run
sleep 2

if ! kill -0 $TSP_PID 2>/dev/null; then
    echo "Error: tsp failed to start. Output:"
    cat tsp_output.log
    exit 1
fi

# 2. Check mlockall (VmLck)
echo "2. Verifying mlockall (Memory Locking)..."
VMLCK=$(grep "VmLck:" /proc/$TSP_PID/status | awk '{print $2, $3}')
if [ -n "$VMLCK" ] && [ "$(echo $VMLCK | awk '{print $1}')" -gt 0 ]; then
    echo "   [PASS] Memory locked: $VMLCK"
else
    echo "   [FAIL] Memory not locked or VmLck not found."
fi

# 3. Check CPU Affinity
echo "3. Verifying CPU Affinity for TX Thread..."
# Find the TID of the transmission thread (usually the second thread)
TID=$(ps -eLo pid,tid,comm | grep $TSP_PID | grep tsp | tail -n 1 | awk '{print $2}')

if [ -n "$TID" ]; then
    AFFINITY=$(taskset -pc $TID | awk '{print $NF}')
    if [ "$AFFINITY" == "$CORE" ]; then
        echo "   [PASS] TX Thread (TID $TID) bound to core $CORE"
    else
        echo "   [FAIL] TX Thread (TID $TID) bound to core $AFFINITY, expected $CORE"
    fi
else
    echo "   [FAIL] Could not find transmission thread TID."
fi

# 4. Check Governor Warning (from log)
echo "4. Checking for Governor/Real-time warnings..."
grep "WARNING" tsp_output.log && echo "   [INFO] Governor warning detected (expected if not in performance mode)" || echo "   [PASS] No governor warnings."

# Cleanup
echo "Cleaning up..."
kill $TSP_PID
rm tsp_output.log
echo "=== Verification Complete ==="
