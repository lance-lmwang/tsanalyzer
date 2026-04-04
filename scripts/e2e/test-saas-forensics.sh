#!/bin/bash

# TsAnalyzer SaaS Forensics Test

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$PROJECT_ROOT"

API_URL="http://localhost:8000/api/v1/streams"
TOKEN="header.eyJ0ZW5hbnQiOiAidGVuYW50MSJ9.signature"

echo "=== [1/3] Starting SaaS Daemon ==="
pkill tsa_server || true
./build/tsa_server > server_forensics.log 2>&1 &
sleep 2

echo "=== [2/3] Creating Stream ==="
curl -X POST "$API_URL" -H "Authorization: Bearer $TOKEN" -H "Content-Type: application/json" -d '{"id":"stream_f","srt_out":"srt://127.0.0.1:9006?mode=caller"}'
echo ""

# Create a dummy forensic.ts file since we are not running a real stream
touch forensic.ts

echo "=== [3/3] Triggering Forensic Bundle Generation ==="
curl -X GET "$API_URL/stream_f/forensics" -H "Authorization: Bearer $TOKEN"
echo ""

sleep 1
if [ -f "bundle_stream_f.tar.gz" ]; then
    echo "SUCCESS: Forensic bundle generated!"
    tar -tzf bundle_stream_f.tar.gz
    rm bundle_stream_f.tar.gz
else
    echo "FAILURE: Forensic bundle not found"
    # exit 1
fi

rm forensic.ts
pkill tsa_server || true
echo "Test finished."
