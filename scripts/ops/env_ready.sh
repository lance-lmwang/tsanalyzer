#!/bin/bash
# libtsshaper Development Environment Setup Script

set -e

echo "[*] Checking Python environment..."

# 1. Check for Python 3
if ! command -v python3 &> /dev/null; then
    echo "[!] Error: Python 3 is not installed."
    exit 1
fi

# 2. Setup Virtual Environment (Optional but recommended)
if [ ! -d "venv" ]; then
    echo "[*] Creating virtual environment 'venv'..."
    python3 -m venv venv
fi

# 3. Activate venv and install dependencies
echo "[*] Installing/Updating Python dependencies from requirements-test.txt..."
source venv/bin/activate
pip install --upgrade pip
pip install -r requirements-test.txt

# 4. Check for system dependencies (DVB analysis)
echo "[*] Checking for TSDuck..."
if ! command -v tsp &> /dev/null; then
    echo "[!] Warning: TSDuck (tsp) not found in PATH."
    echo "    Please install it from https://tsduck.io/ for full TR 101 290 compliance testing."
fi

echo "[+] Environment is READY. To start testing, run: source venv/bin/activate"
