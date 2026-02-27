#!/bin/bash

# TsAnalyzer Monitoring Stack Startup Script
# This starts Prometheus and Grafana in the background.

MONITORING_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$MONITORING_DIR"

# Load environment variables from .env file if it exists
if [ -f .env ]; then
    set -a
    source .env
    set +a
fi

echo "Starting TsAnalyzer Monitoring Stack (Prometheus & Grafana)..."
docker compose up -d

if [ $? -eq 0 ]; then
    echo "-------------------------------------------------------"
    echo "Monitoring Stack is UP!"
    echo "Grafana Dashboard: http://localhost:${GRAFANA_PORT:-3000}"
    echo "Prometheus UI:     http://localhost:${PROMETHEUS_PORT:-9090}"
    echo "-------------------------------------------------------"
else
    echo "Failed to start monitoring stack."
    exit 1
fi
