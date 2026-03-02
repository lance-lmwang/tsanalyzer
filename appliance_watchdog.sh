#!/bin/bash
# TsAnalyzer Pro - APPLIANCE WATCHDOG (v1.0)
# ------------------------------------------------------------------------------
LOG_FILE="watchdog.log"
PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$PROJECT_ROOT"

echo "[$(date)] Watchdog Started." >> "$LOG_FILE"

while true; do
    TIMESTAMP=$(date "+%Y-%m-%d %H:%M:%S")
    STATUS="OK"

    # 1. Check Engine (8088)
    if ! pgrep -x "tsa_server" > /dev/null; then
        echo "[$TIMESTAMP] ❌ CRITICAL: Engine (tsa_server) is down. Restarting..." >> "$LOG_FILE"
        ./build/tsa_server tsa.conf > server_auto_recovery.log 2>&1 &
        STATUS="RECOVERING"
        sleep 2
    fi

    # 2. Check Streams (8)
    STREAM_COUNT=$(pgrep -x "tsp" | wc -l)
    if [ "$STREAM_COUNT" -lt 8 ]; then
        echo "[$TIMESTAMP] ⚠️ WARNING: Streams down ($STREAM_COUNT/8). Re-injecting..." >> "$LOG_FILE"
        pkill -9 tsp || true
        for i in {1..8}; do
            ./build/tsp -i 127.0.0.1 -p $((19000+i)) -l -f sample/cctv5.ts -b 2000000 > /dev/null 2>&1 &
        done
        STATUS="RECOVERING"
    fi

    # 3. Check Monitoring Stack (Docker)
    DOCKER_DOWN=$(docker ps --format "{{.Names}}" | grep -E "tsa-grafana|tsa-prometheus" | wc -l)
    if [ "$DOCKER_DOWN" -lt 2 ]; then
        echo "[$TIMESTAMP] ❌ CRITICAL: Docker Monitoring Stack is down. Restarting..." >> "$LOG_FILE"
        cd monitoring && docker compose up -d && cd ..
        STATUS="RECOVERING"
    fi

    # 4. Final Verification
    if [ "$STATUS" == "OK" ]; then
        echo "[$TIMESTAMP] ✅ System Healthy. (Engine: UP, Streams: 8, Monitoring: UP)" >> "$LOG_FILE"
    fi

    # Wait for 5 minutes
    sleep 300
done
