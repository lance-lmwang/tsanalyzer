#!/bin/bash

# Configuration
BIN="./build/tspacer"
PID_FILE="tspacer.pid"

echo "=== SIGINT Response Test ==="

if [ ! -f "$BIN" ]; then
    echo "Error: tspacer binary not found. Please build it first."
    exit 1
fi

# 1. Start tspacer in background (reading from stdin effectively)
# We don't provide -f, so it defaults to stdin.
# We redirect stdin from /dev/zero to simulate a stream, or just pipe something that hangs?
# The issue is when reading from stdin (e.g. pipe or terminal).
# If we pipe /dev/zero, fread won't block.
# We need to simulate a blocked stdin. We can pipe from a named pipe that we don't write to.

LOG="/tmp/tspacer.log"
echo "1. Starting tspacer with blocked stdin (piped sleep)..."
# Pipe sleep output (empty, infinite) to tspacer
sleep 1000 | $BIN -b 1000000 -i 127.0.0.1 -p 1234 > $LOG 2>&1 &
TSP_PID=$!
echo $TSP_PID > $PID_FILE

# Check if running
sleep 1
if ! kill -0 $TSP_PID 2>/dev/null; then
    echo "Error: tspacer failed to start."
    exit 1
fi

echo "   tspacer running with PID $TSP_PID"

# 2. Send SIGINT
echo "2. Sending SIGINT..."
kill -INT $TSP_PID

# 3. Wait and check
echo "3. Waiting 2 seconds for exit..."
sleep 2

if kill -0 $TSP_PID 2>/dev/null; then
    echo "   [FAIL] Process $TSP_PID still running. SIGINT ignored or blocked."
    ls -l $LOG
    echo "--- tspacer.log ---"
    cat $LOG
    echo "-------------------"
    kill -9 $TSP_PID
    exit 1
else
    echo "   [PASS] Process terminated successfully."
    exit 0
fi
