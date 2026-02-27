#!/bin/bash

# TsAnalyzer Monitoring Stack Shutdown Script

MONITORING_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$MONITORING_DIR"

echo "Stopping TsAnalyzer Monitoring Stack..."
docker compose down

if [ $? -eq 0 ]; then
    echo "Monitoring Stack is DOWN."
else
    echo "Failed to stop monitoring stack properly."
    exit 1
fi
