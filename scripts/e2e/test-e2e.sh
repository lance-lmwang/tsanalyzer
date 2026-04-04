#!/bin/bash

# ==============================================================================
# TsAnalyzer Pro: Comprehensive E2E Acceptance Test Suite
#
# This script acts as the final quality gate. It MUST pass before any
# production deployment. It validates:
#   1. SRT-AES Transport Integrity
#   2. SaaS API Lifecycle (Create, Update, Delete)
#   3. Gateway Failsafe & Metrics
# ==============================================================================

set -e  # Exit immediately if a command exits with a non-zero status.
PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$PROJECT_ROOT"

# --- Test Parameters ---
API_URL="http://localhost:8088/api/v1/streams"
SAMPLE_FILE="./sample/test.ts"
[ ! -f "$SAMPLE_FILE" ] && SAMPLE_FILE="../../sample/test.ts"
[ ! -f "$SAMPLE_FILE" ] && SAMPLE_FILE="/home/lmwang/dev/sample/test.ts"
LOG_DIR="build/e2e_logs"

# --- Security: Dynamic JWT Generation ---
# The server uses "tsanalyzer-default-secret" if TSA_API_SECRET env is not set.
SECRET="tsanalyzer-default-secret"
HEADER_B64="eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9"
PAYLOAD_B64="eyJ0ZW5hbnQiOiAiZDJlMi10ZXN0In0"

# Compute HMAC signature using OpenSSL
SIGNATURE=$(echo -n "${HEADER_B64}.${PAYLOAD_B64}" | openssl dgst -sha256 -hmac "$SECRET" -binary | base64 | tr '+/' '-_' | tr -d '=')
TOKEN="${HEADER_B64}.${PAYLOAD_B64}.${SIGNATURE}"

# --- Helper Functions ---
function print_header {
    echo ""
    echo "=============================================================================="
    echo "  [E2E] $1"
    echo "=============================================================================="
}

function assert_success {
    if [ $? -ne 0 ]; then
        echo "  [FAIL] Previous command failed. Aborting."
        exit 1
    fi
    echo "  [OK] Success."
}

# --- Test Execution ---
print_header "SETUP: Cleaning up environment"
pkill -9 -x tsa_server || true
pkill -9 -x tsa_server_pro || true
pkill -9 -x tsa_cli || true
pkill -9 -x tsp || true
pkill -9 -x tsg || true
rm -rf "$LOG_DIR" && mkdir -p "$LOG_DIR"
sleep 2

print_header "PHASE 1: SaaS API & SRT-AES Transport Test"
echo "-> Starting SaaS Daemon in background..."
# Create a minimal config to ensure the HTTP server starts
cat << EOF > build/e2e_config.conf
{
    "http_port": 8088,
    "metrics_path": "/metrics",
    "expert_mode": true,
    "nodes": []
}
EOF
./build/tsa_server_pro build/e2e_config.conf > "$LOG_DIR/saas_daemon.log" 2>&1 &
SAAS_PID=$!

echo "-> Waiting for SaaS API to become ready..."
MAX_WAIT=15
READY=0
for i in $(seq 1 $MAX_WAIT); do
    if curl -s http://localhost:8088/api/v1/streams > /dev/null; then
        READY=1
        break
    fi
    echo "   Attempt $i/$MAX_WAIT: still waiting..."
    sleep 1
done

if [ $READY -eq 0 ]; then
    echo "[FAIL] FATAL: Server failed to start or bind to port 8088 within ${MAX_WAIT}s"
    cat "$LOG_DIR/saas_daemon.log"
    exit 1
fi
echo "   [PASS] API Ready."

echo "-> Creating secure stream 'e2e_stream_1' via API..."
curl -s --connect-timeout 5 --max-time 10 -X POST "$API_URL" \
    -H "Authorization: Bearer $TOKEN" \
    -H "Content-Type: application/json" \
    -d '{"id":"e2e_stream_1","srt_out":"srt://:9100?mode=listener&passphrase=e2e-secret"}' > "$LOG_DIR/api_create.log"
assert_success
cat "$LOG_DIR/api_create.log" | jq
assert_success

echo "-> Pushing traffic with matching secret..."
./build/tsp -P --srt-url "srt://127.0.0.1:9100?mode=caller&passphrase=e2e-secret" -l -f "$SAMPLE_FILE" > "$LOG_DIR/pacer_aes.log" 2>&1 &
TSP_PID=$!
sleep 5

echo "-> Verifying stream is active..."
RESPONSE=$(curl -s -X GET "$API_URL" -H "Authorization: Bearer $TOKEN")
if echo "$RESPONSE" | jq -e '.streams' > /dev/null 2>&1; then
    echo "$RESPONSE" | jq '.streams[]' | grep -q "e2e_stream_1"
    assert_success
else
    echo "  [FAIL] Invalid API response: $RESPONSE"
    exit 1
fi

pkill -P $TSP_PID || true

print_header "PHASE 2: Gateway Metrics & Fail-safe Test"
echo "-> Starting Gateway with real-time metrics..."
./build/tsg --srt-in "srt://:9101?mode=listener" --dest-ip 127.0.0.1 --dest-port 1240 --http "http://127.0.0.1:8001" > "$LOG_DIR/gateway_metrics.log" 2>&1 &
TSG_PID=$!
sleep 2

echo "-> Pushing traffic to Gateway..."
./build/tsp -P --srt-url "srt://127.0.0.1:9101?mode=caller" -l -f "$SAMPLE_FILE" > "$LOG_DIR/pacer_gw.log" 2>&1 &
TSP_GW_PID=$!
sleep 5

echo "-> Verifying metrics endpoint..."
curl -s http://127.0.0.1:8001/metrics | grep "tsa_tr101290_p1_cc_errors_total"
assert_success

pkill -P $TSG_PID || true
pkill -P $TSP_GW_PID || true

print_header "PHASE 3: Cleanup"
echo "-> Deleting stream via API..."
curl -s -X DELETE "$API_URL/e2e_stream_1" -H "Authorization: Bearer $TOKEN" | jq
assert_success

pkill -P $SAAS_PID || true
echo "-> All processes terminated."

print_header "FINAL RESULT: Comprehensive E2E Test Suite PASSED"
exit 0
