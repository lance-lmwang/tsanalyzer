#!/bin/bash
PORT=8088
DEPLOY_DIR=".tmp_dashboard"
SOURCE_FILE="mock_noc_dashboard.html"

echo "[1/5] Force Cleaning Environment..."
fuser -k $PORT/tcp 2>/dev/null || true
sleep 1
rm -rf $DEPLOY_DIR && mkdir -p $DEPLOY_DIR

echo "[2/5] Preparing Assets (v1.2 Full NOC)..."
if [ ! -f "$SOURCE_FILE" ]; then
    echo "FATAL: $SOURCE_FILE not found!"
    exit 1
fi
cp "$SOURCE_FILE" "$DEPLOY_DIR/index.html"

echo "[3/5] Starting Background Server..."
cd $DEPLOY_DIR
# Using python3 -u for unbuffered output
nohup python3 -u -m http.server $PORT > ../server_access.log 2>&1 &
SERVER_PID=$!
cd ..

echo "[4/5] Settling and Self-Testing..."
sleep 5

# Check Process
if ! ps -p $SERVER_PID > /dev/null; then
    echo "FATAL: Server PID $SERVER_PID died immediately."
    exit 1
fi

# Check Content
HTTP_STATUS=$(curl -s -o /dev/null -w "%{http_code}" http://localhost:$PORT)
KW_CHECK=$(curl -s http://localhost:$PORT | grep "TSANALYZER PRO")

if [[ "$HTTP_STATUS" == "200" && -n "$KW_CHECK" ]]; then
    echo "SUCCESS: 3000 is LIVE. Content 'TSANALYZER PRO' verified."
else
    echo "FATAL: Verification Failed. Status: $HTTP_STATUS, Keyword Found: $(if [ -n "$KW_CHECK" ]; then echo "Yes"; else echo "No"; fi)"
    exit 1
fi

echo "[5/5] Deployment Stable. Serving at http://$(hostname -I | awk '{print $1}'):$PORT"
lsof -i :$PORT
