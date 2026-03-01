#!/bin/bash
# One-click build and verify script
set -e

echo ">>> Initializing Professional Build..."
make clean
make all

echo ">>> Running Integration Verification..."
make full-test

echo ""
echo ">>> SUCCESS: TsAnalyzer is ready for production deployment."
