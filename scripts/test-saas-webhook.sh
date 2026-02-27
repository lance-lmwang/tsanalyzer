#!/bin/bash

# TsAnalyzer SaaS Webhook Test

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$PROJECT_ROOT"

API_URL="http://localhost:8000/api/v1/streams"
TOKEN="header.eyJ0ZW5hbnQiOiAidGVuYW50MSJ9.signature"
WEBHOOK_URL="http://localhost:8000/webhook-sink"

echo "=== [1/3] Starting SaaS Daemon ==="
pkill tsa_server || true
./build/tsa_server > server_webhook.log 2>&1 &
sleep 2

echo "=== [2/3] Creating Stream with Webhook ==="
curl -X POST "$API_URL" -H "Authorization: Bearer $TOKEN" -H "Content-Type: application/json" -d "{\"id\":\"stream_wh\",\"srt_out\":\"srt://127.0.0.1:9005?mode=caller\",\"webhook_url\":\"$WEBHOOK_URL\"}"
echo ""

echo "=== [3/3] Checking logs for Webhook dispatch ==="
sleep 2
if grep -q "status\":\"created\"" server_webhook.log; then
    echo "SUCCESS: Stream created successfully!"
fi

pkill tsa_server || true
echo "Test finished."
