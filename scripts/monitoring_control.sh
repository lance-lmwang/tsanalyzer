#!/bin/bash
# TsAnalyzer Pro - Monitoring stack one-click purge and restart script (Auto-Purge)

MONITORING_DIR="monitoring"

function cleanup_all() {
    echo "[1/4] Forcefully purging old data and containers..."
    cd $MONITORING_DIR
    docker compose down -v > /dev/null 2>&1
    cd ..
    pkill -9 tsa_server > /dev/null 2>&1
    pkill -9 tsp > /dev/null 2>&1
}

function start_stack() {
    echo "[2/4] Starting fresh monitoring stack..."
    cd $MONITORING_DIR
    docker compose up -d > /dev/null 2>&1
    cd ..

    # Waiting for Grafana ready
    echo "Waiting for Grafana service initialization..."
    until curl -s http://localhost:3000/api/health | grep "ok" > /dev/null; do
        sleep 1
    done
}

function start_data_flow() {
    echo "[3/4] Starting high-performance analysis engine (tsa_server)..."
    ./build/tsa_server > server.log 2>&1 &
    sleep 2

    echo "[4/4] Injecting 8-stream test set (10 Mbps)..."
    SAMPLE_FILE="/tmp/dummy.ts"
    if [ ! -f "$SAMPLE_FILE" ]; then
        dd if=/dev/zero bs=188 count=10000 > "$SAMPLE_FILE" 2>/dev/null
        for i in {0..9999}; do printf '\x47' | dd of="$SAMPLE_FILE" bs=1 seek=$((i*188)) conv=notrunc 2>/dev/null; done
    fi

    for i in {1..8}; do
        ./build/tsp -i 127.0.0.1 -p $((19001 + i - 1)) -l -f "$SAMPLE_FILE" -b 10000000 > /dev/null 2>&1 &
    done
}

function verify() {
    echo "-------------------------------------------------------"
    echo "Restart complete! Check results: "
    echo "- Grafana: http://localhost:3000"
    echo "- Metrics: http://localhost:8088/metrics"
    echo "- Dashboard Status: $(curl -s http://localhost:3000/api/search?type=dash-db | grep -o '"title":"[^"]*"' | head -n 1)"
    echo "- Scrape Status: $(curl -s http://localhost:9090/api/v1/targets | grep -o '"health":"up"' | head -n 1)"
    echo "-------------------------------------------------------"
}

cleanup_all
start_stack
start_data_flow
sleep 5
verify
