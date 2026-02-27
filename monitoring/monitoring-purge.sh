#!/bin/bash
set -uo pipefail

# TsAnalyzer Monitoring Stack: Environment Purge Script
# Usage: ./monitoring-purge.sh [--all]

MONITORING_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$MONITORING_DIR"

WIPE_VOLUMES=""
if [[ "${1:-}" == "--all" ]]; then
    WIPE_VOLUMES="-v"
    echo "!!! Full wipe requested: All persistent data will be deleted !!!"
fi

echo "=== TsAnalyzer Monitoring: Environment Purge ==="

# 1. Stop containers
echo "[*] Stopping containers..."
docker compose down $WIPE_VOLUMES

# 2. Reset targets to local default if they were missing or corrupt
if [ ! -f prometheus/targets.json ] || [ "$WIPE_VOLUMES" != "" ]; then
    echo "[*] Resetting Prometheus targets to default..."
    cat > prometheus/targets.json <<EOF
[
  {
    "targets": ["host.docker.internal:8000"],
    "labels": {
      "instance": "local-probe"
    }
  }
]
EOF
fi

# 3. Clean up log files
echo "[*] Cleaning up analysis logs..."
rm -f ../analyzer_e2e.log ../pacer_e2e.log

echo "[SUCCESS] Environment cleaned."
