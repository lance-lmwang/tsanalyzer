#!/bin/bash
# TsAnalyzer - Environment Readiness & Purge Tool (v4.0)
# Purpose: Ensures a 100% clean state for API Port 8088 and Monitoring Port 3000.

echo "===================================================="
echo "   TSANALYZER ENVIRONMENT READINESS CHECK          "
echo "===================================================="

# 1. Kill all potential backend/data-pump processes
echo "[1/4] Stopping all TsAnalyzer processes..."
pkill -9 tsa_server_pro 2>/dev/null
pkill -9 tsa_server 2>/dev/null
pkill -9 tsp 2>/dev/null
pkill -9 tsa_cli 2>/dev/null
pkill -9 test_server 2>/dev/null
sleep 1

# 2. Shutdown and Purge Docker Monitoring Stack
echo "[2/4] Tearing down Docker monitoring stack..."
if [ -f "monitoring/docker-compose.yml" ]; then
    docker compose -f monitoring/docker-compose.yml down -v --remove-orphans 2>/dev/null
else
    echo "  -> Warning: monitoring/docker-compose.yml not found."
fi

# 3. Forceful port reclamation
echo "[3/4] Reclaiming standardized ports (8088, 9090, 3000)..."
PORTS=(8088 9090 3000)
for port in "${PORTS[@]}"; do
    fuser -k -9 ${port}/tcp 2>/dev/null && echo "  -> Freed port $port"
done

# 4. Final Audit
echo "[4/4] Final Audit Report:"
ERRORS=0

# Check Processes
PROC_COUNT=$(ps aux | grep -E "tsa_server|tsp|prometheus|grafana" | grep -v grep | wc -l)
if [ "$PROC_COUNT" -eq 0 ]; then
    echo "  [PASS] Processes: Clean"
else
    echo "  [FAIL] Processes: $PROC_COUNT still active!"
    ERRORS=$((ERRORS+1))
fi

# Check Ports
for port in "${PORTS[@]}"; do
    if ! netstat -tulpn 2>/dev/null | grep -q ":${port} "; then
        echo "  [PASS] Port $port: Free"
    else
        echo "  [FAIL] Port $port: Still occupied!"
        ERRORS=$((ERRORS+1))
    fi
done

echo "----------------------------------------------------"
if [ $ERRORS -eq 0 ]; then
    echo "RESULT: ENVIRONMENT IS READY (100% CLEAN)"
    exit 0
else
    echo "RESULT: CLEANUP FAILED ($ERRORS issues remaining)"
    exit 1
fi
