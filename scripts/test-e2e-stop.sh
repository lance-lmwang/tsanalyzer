#!/bin/bash

# TsPacer/TsAnalyzer E2E Cleanup Script

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$PROJECT_ROOT"

echo "Stopping Pacer and Analyzer..."
[ -f .e2e_pacer.pid ] && kill -TERM $(cat .e2e_pacer.pid) && rm .e2e_pacer.pid
[ -f .e2e_analyzer.pid ] && kill -TERM $(cat .e2e_analyzer.pid) && rm .e2e_analyzer.pid

pkill -TERM tsa || true
pkill -TERM tsp || true

echo "Stopping Monitoring Stack..."
cd monitoring && ./monitoring-down.sh && cd ..

echo "Cleanup complete."
