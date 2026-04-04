#!/bin/bash
# TsAnalyzer Pro - Appliance Integrity Acceptance Test (v5.5.6)
# ------------------------------------------------------------------------------

set -e

if [ -f "appliance.env" ]; then
    source appliance.env
else
    TSA_PORT=8088
    PROM_PORT=9090
fi

TSA_METRICS="http://localhost:$TSA_PORT/metrics"
PROM_API="http://localhost:$PROM_PORT/api/v1"
GRAFANA_API="http://localhost:3000/api"

echo "================================================================================"
echo "TsAnalyzer Pro - APPLIANCE INTEGRITY ACCEPTANCE TEST (Port: $TSA_PORT)"
echo "================================================================================"

echo "[*] Waiting 15s for data metrics to stabilize..."
sleep 15

# 1. Verify Engine (Source)
echo -n "[1/4] Probing TSA Engine Metrics at :$TSA_PORT... "
HTTP_STATUS=$(curl -s -o /dev/null -w "%{http_code}" "$TSA_METRICS" || echo "000")

if [ "$HTTP_STATUS" == "200" ]; then
    if curl -s "$TSA_METRICS" | grep -q "tsa_system_health_score"; then
        echo "[PASS] PASS"
    else
        echo "[FAIL] FAIL (Server is UP at :$TSA_PORT, but NO DATA. Is 'tsp' running?)"
        exit 1
    fi
else
    echo "[FAIL] FAIL (Server is DOWN at :$TSA_PORT. Error Code: $HTTP_STATUS)"
    if lsof -Pi :$TSA_PORT -sTCP:LISTEN -t >/dev/null ; then
        CONFLICT_PROC=$(ps -p $(lsof -t -i:$TSA_PORT) -o comm=)
        echo "    CONFLICT DETECTED: Port $TSA_PORT is occupied by process: '$CONFLICT_PROC'"
    fi
    exit 1
fi

# 2. Verify Transport (Transport)
echo -n "[2/4] Verifying Prometheus Scrape Success... "
SCRAPE_STATE=$(curl -s "$PROM_API/targets" | jq -r ".data.activeTargets[] | select(.discoveredLabels.__address__==\"127.0.0.1:$TSA_PORT\") | .health" 2>/dev/null || echo "unknown")
if [ "$SCRAPE_STATE" == "up" ]; then
    echo "[PASS] PASS"
else
    echo "[FAIL] FAIL (Prometheus reports target is $SCRAPE_STATE)"
    echo "    Tip: Check http://localhost:$PROM_PORT/targets"
    exit 1
fi

# 3. Verify Logic (Logic)
echo -n "[3/4] Testing Failure Domain Inference Rules... "
INFERENCE_VALUE=$(curl -s "$PROM_API/query?query=dominant_failure_domain" | jq -r '.data.result[0].value[1]' 2>/dev/null || echo "null")
if [ "$INFERENCE_VALUE" != "null" ]; then
    echo "[PASS] PASS (Brain is calculating states)"
else
    echo "[FAIL] FAIL (Inference metric is empty)"
    exit 1
fi

# 4. Verify UI (Presentation)
echo -n "[4/4] Verifying Grafana API Accessibility... "
if curl -s -o /dev/null -w "%{http_code}" "$GRAFANA_API/health" | grep -q "200"; then
    echo "[PASS] PASS"
else
    echo "[FAIL] FAIL (Grafana unreachable at :3000)"
    exit 1
fi

echo "================================================================================"
echo "[PASS] APPLIANCE STATUS: OPERATIONAL"
echo "URL: http://$(hostname -I | awk '{print $1}'):3000/d/global-wall"
echo "================================================================================"
