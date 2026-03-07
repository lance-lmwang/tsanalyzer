#!/bin/bash
# TSA Pro NOC E2E Verification Script

PORT=8088

echo "=== [1/4] Building Project ==="
./build.sh || exit 1

# Cleanup previous instances
fuser -k $PORT/tcp 2>/dev/null || true
pkill tsa_server 2>/dev/null || true
sleep 1

echo "=== [2/4] Starting TSA Pro NOC Server ($PORT) ==="
./build/tsa_server http://0.0.0.0:$PORT > server.log 2>&1 &
SERVER_PID=$!
sleep 2

echo "=== [3/4] Registering Mock Stream === "
curl -s -X POST -H "Content-Type: application/json" -d '{"stream_id":"poc-test","url":"udp://127.0.0.1:9001"}' http://localhost:$PORT/api/v1/config/streams

echo "=== [4/4] Verifying Dashboard API ==="
sleep 2
RESPONSE=$(curl -s "http://localhost:$PORT/api/v1/snapshot?id=poc-test")

if echo "$RESPONSE" | grep -q "master_health"; then
    echo "SUCCESS: NOC API is live!"
    echo "JSON Snapshot:"
    echo "$RESPONSE" | jq . 2>/dev/null || echo "$RESPONSE"
    echo ""
    echo "View Dashboard at: http://localhost:$PORT/"
else
    echo "FAILURE: API response invalid."
    echo "Response: $RESPONSE"
    kill $SERVER_PID 2>/dev/null
    exit 1
fi

echo ""
echo "To stop the server, run: kill $SERVER_PID"
echo "------------------------------------------------"
