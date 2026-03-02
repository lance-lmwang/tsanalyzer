#!/bin/bash
# TsAnalyzer Pro - Appliance Integrity Acceptance Test (v5.5.5)
# ------------------------------------------------------------------------------
# 职责: 验证 8082 (Engine) -> 9090 (Prom) -> 3000 (Grafana) 全链路数据流。
# ------------------------------------------------------------------------------

set -e

# 严格对齐 8082 端口
TSA_METRICS="http://localhost:8082/metrics"
PROM_API="http://localhost:9090/api/v1"
GRAFANA_API="http://localhost:3000/api"

echo "================================================================================"
echo "TsAnalyzer Pro - APPLIANCE INTEGRITY ACCEPTANCE TEST (Port: 8082)"
echo "================================================================================"

echo "[*] Waiting 15s for data metrics to stabilize..."
sleep 15

# 1. 验证引擎 (Source)
echo -n "[1/4] Probing TSA Engine Metrics at :8082... "
HTTP_STATUS=$(curl -s -o /dev/null -w "%{http_code}" "$TSA_METRICS" || echo "000")

if [ "$HTTP_STATUS" == "200" ]; then
    if curl -s "$TSA_METRICS" | grep -q "tsa_health_score"; then
        echo "✅ PASS"
    else
        echo "❌ FAIL (Server is UP at :8082, but NO DATA. Is 'tsp' running?)"
        exit 1
    fi
else
    echo "❌ FAIL (Server is DOWN at :8082. Error Code: $HTTP_STATUS)"
    echo "    Check server.log for bind errors."
    exit 1
fi

# 2. 验证抓取 (Transport)
echo -n "[2/4] Verifying Prometheus Scrape Success... "
# 在 Host 模式下，直接查目标状态
SCRAPE_STATE=$(curl -s "$PROM_API/targets" | jq -r '.data.activeTargets[] | select(.discoveredLabels.__address__=="127.0.0.1:8082") | .health' 2>/dev/null || echo "unknown")
if [ "$SCRAPE_STATE" == "up" ]; then
    echo "✅ PASS"
else
    echo "❌ FAIL (Prometheus reports target is $SCRAPE_STATE)"
    echo "    Tip: Check http://localhost:9090/targets"
    exit 1
fi

# 3. 验证推理 (Logic)
echo -n "[3/4] Testing Failure Domain Inference Rules... "
INFERENCE_VALUE=$(curl -s "$PROM_API/query?query=dominant_failure_domain" | jq -r '.data.result[0].value[1]' 2>/dev/null || echo "null")
if [ "$INFERENCE_VALUE" != "null" ]; then
    echo "✅ PASS (Brain is calculating states)"
else
    echo "❌ FAIL (Inference metric is empty)"
    exit 1
fi

# 4. 验证仪表盘 (UI)
echo -n "[4/4] Checking Dashboard Availability... "
if curl -s -o /dev/null -w "%{http_code}" "$GRAFANA_API/dashboards/uid/global-wall" | grep -q "200"; then
    echo "✅ PASS"
else
    echo "❌ FAIL (Dashboard 'global-wall' not found)"
    exit 1
fi

echo "================================================================================"
echo "✅ APPLIANCE STATUS: OPERATIONAL"
echo "URL: http://192.168.1.127:3000/d/global-wall"
echo "================================================================================"
