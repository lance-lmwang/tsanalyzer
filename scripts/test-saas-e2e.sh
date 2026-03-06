#!/bin/bash

# TsAnalyzer SaaS Multi-Tenant E2E Test

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$PROJECT_ROOT"

API_URL="http://localhost:8088/api/v1/streams"
SAMPLE_FILE="sample/test.ts"

# JWTs for two tenants
TOKEN1="header.eyJ0ZW5hbnQiOiAidGVuYW50MSJ9.signature"
TOKEN2="header.eyJ0ZW5hbnQiOiAidGVuYW50MiJ9.signature"

echo "=== [1/5] Starting SaaS Daemon ==="
pkill tsa_server || true
pkill tsg || true
pkill tsp || true
./build/tsa_server > server_e2e.log 2>&1 &
sleep 2

echo "=== [2/5] Creating Multi-Tenant Streams ==="
# Stream 1 for Tenant 1
curl -X POST "$API_URL" -H "Authorization: Bearer $TOKEN1" -H "Content-Type: application/json" -d '{"id":"stream1","srt_out":"srt://:9011?mode=listener"}'
echo ""

# Stream 2 for Tenant 2
curl -X POST "$API_URL" -H "Authorization: Bearer $TOKEN2" -H "Content-Type: application/json" -d '{"id":"stream2","srt_out":"srt://:9012?mode=listener"}'
echo ""

echo "=== [3/5] Pushing Traffic to Both Streams ==="
# Push to Stream 1 (Tenant 1)
nohup ./build/tsp -P --srt-url "srt://127.0.0.1:9011?mode=caller" -l -f "$SAMPLE_FILE" > pacer1.log 2>&1 &
# Push to Stream 2 (Tenant 2)
nohup ./build/tsp -P --srt-url "srt://127.0.0.1:9012?mode=caller" -l -f "$SAMPLE_FILE" > pacer2.log 2>&1 &

sleep 5

echo "=== [4/5] Verifying Isolation and Metrics ==="
# Get streams for Tenant 1
echo "Tenant 1 Streams:"
curl -s "$API_URL" -H "Authorization: Bearer $TOKEN1"
echo ""

# Get streams for Tenant 2
echo "Tenant 2 Streams:"
curl -s "$API_URL" -H "Authorization: Bearer $TOKEN2"
echo ""

echo "=== [5/5] Testing Forensics for Both ==="
# Create dummy forensic.ts
touch forensic.ts
curl -X GET "$API_URL/stream1/forensics" -H "Authorization: Bearer $TOKEN1"
echo ""
curl -X GET "$API_URL/stream2/forensics" -H "Authorization: Bearer $TOKEN2"
echo ""

sleep 1
ls -l bundle_*.tar.gz

echo "Cleanup..."
pkill tsa_server || true
pkill tsp || true
rm bundle_*.tar.gz forensic.ts manifest.json
echo "E2E Test Finished."
