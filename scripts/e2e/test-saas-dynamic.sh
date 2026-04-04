#!/bin/bash

# TsAnalyzer SaaS Dynamic Policy Update Test

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$PROJECT_ROOT"

API_URL="http://localhost:8000/api/v1/streams"
# Dummy JWT with tenant=tenant1 (base64 encoded {"tenant": "tenant1"} -> eyJ0ZW5hbnQiOiAidGVuYW50MSJ9)
TOKEN="header.eyJ0ZW5hbnQiOiAidGVuYW50MSJ9.signature"

echo "=== [1/4] Starting SaaS Daemon ==="
pkill tsa_server || true
./build/tsa_server > server.log 2>&1 &
SERVER_PID=$!
sleep 2

echo "=== [2/4] Creating Stream via API ==="
curl -X POST "$API_URL" -H "Authorization: Bearer $TOKEN" -H "Content-Type: application/json" -d '{"id":"stream1","srt_out":"srt://127.0.0.1:9003?mode=caller"}'
echo ""

echo "=== [3/4] Updating Security Policy (PATCH) ==="
curl -X PATCH "$API_URL/stream1" -H "Authorization: Bearer $TOKEN" -H "Content-Type: application/json" -d '{"srt_out":"srt://127.0.0.1:9003?mode=caller&passphrase=new-secret"}'
echo ""

echo "=== [4/4] Verifying Cleanup ==="
curl -X DELETE "$API_URL/stream1" -H "Authorization: Bearer $TOKEN"
echo ""

pkill tsa_server || true
echo "Test finished."
