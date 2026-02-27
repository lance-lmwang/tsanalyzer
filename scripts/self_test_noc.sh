#!/bin/bash
# TsAnalyzer Pro NOC Self-Test Script (v2)

PORT=8080
LAN_IP="192.168.7.2"
SUCCESS=0

echo "=== STARTING NOC SERVER SELF-TEST (v2) ==="

# 1. Start Server
fuser -k $PORT/tcp 2>/dev/null || true
./build/tsa_server http://0.0.0.0:$PORT > server.log 2>&1 &
SERVER_PID=$!
sleep 3

# 2. Verify Process
if ps -p $SERVER_PID > /dev/null; then
    echo "[PASS] Server Process is alive (PID: $SERVER_PID)"
else
    echo "[FAIL] Server crashed on startup."
    exit 1
fi

# 3. Test Loopback
if curl -s -I http://127.0.0.1:$PORT/ | grep -q "200 OK"; then
    echo "[PASS] Loopback (127.0.0.1) is responding."
else
    echo "[FAIL] Loopback is not responding."
    SUCCESS=1
fi

# 4. Test LAN IP
if curl -s -I http://$LAN_IP:$PORT/ | grep -q "200 OK"; then
    echo "[PASS] LAN IP ($LAN_IP) is responding."
else
    echo "[FAIL] LAN IP ($LAN_IP) is NOT responding."
    SUCCESS=1
fi

# 5. Test API Registration (Robust version)
JSON_DATA='{"stream_id":"SELF-TEST","url":"udp://127.0.0.1:9001"}'
REG=$(curl -s -X POST -H "Content-Type: application/json" -d "$JSON_DATA" "http://127.0.0.1:$PORT/api/v1/config/streams")

if echo "$REG" | grep -q "created"; then
    echo "[PASS] Stream Registration API is functional."
else
    echo "[FAIL] API Registration failed: $REG"
    SUCCESS=1
fi

# 6. Final Verdict
if [ $SUCCESS -eq 0 ]; then
    echo "======================================"
    echo "VERDICT: NOC SERVER IS READY"
    echo "URL: http://$LAN_IP:$PORT/"
    echo "======================================"
else
    echo "======================================"
    echo "VERDICT: SELF-TEST FAILED"
    echo "======================================"
    kill $SERVER_PID
    exit 1
fi
