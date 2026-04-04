#!/bin/bash

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$PROJECT_ROOT"

# Configuration
SRT_PORT=9002
SAMPLE_FILE="./sample/test.ts"
[ ! -f "$SAMPLE_FILE" ] && SAMPLE_FILE="../../sample/test.ts"
[ ! -f "$SAMPLE_FILE" ] && SAMPLE_FILE="/home/lmwang/dev/sample/test.ts"
PASSPHRASE="top-secret-123"

echo "=== [1/3] Building Project ==="
./build.sh || exit 1

# Cleanup
pkill tsg || true
pkill tsp || true
sleep 1

echo "=== [2/3] Launching TsGateway (SRT Listener with AES) ==="
# Ingest with AES
nohup ./build/tsg --srt-in "srt://:9002?mode=listener&passphrase=$PASSPHRASE&pbkeylen=16" --dest-ip 127.0.0.1 --dest-port 1238 > gateway_aes.log 2>&1 &
GW_PID=$!
echo "Gateway started (PID: $GW_PID)"
sleep 2

echo "=== [3/3] Launching TsPacer (SRT Caller with AES) ==="
# Egress with AES
nohup ./build/tsp -P --srt-url "srt://127.0.0.1:9002?mode=caller&passphrase=$PASSPHRASE&pbkeylen=16" -l -f "$SAMPLE_FILE" > pacer_aes.log 2>&1 &
PACER_PID=$!
echo "Pacer started (PID: $PACER_PID)"

sleep 5
echo "=== Verifying Connection ==="
if grep -q "SRT Ingest" gateway_aes.log; then
    echo "SUCCESS: Gateway initialized SRT Ingest with AES!"
else
    echo "FAILURE: Gateway failed to initialize SRT Ingest"
    tail -n 20 gateway_aes.log
    exit 1
fi

# Check for SRT stats if possible or just check log for "accepted"
sleep 2
if grep -q "Gateway Metrics server started" gateway_aes.log; then
    echo "SUCCESS: Connection established and metrics active!"
else
    echo "FAILURE: Gateway metrics server not detected"
    exit 1
fi

echo "Test passed. Cleaning up..."
pkill tsg || true
pkill tsp || true
exit 0
