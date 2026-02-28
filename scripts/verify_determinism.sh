#!/bin/bash
set -e

TS_FILE=$1
if [ -z "$TS_FILE" ]; then
    echo "Usage: $0 <test.ts>"
    exit 1
fi

echo "--- Running Run 1 ---"
./build/tsa "$TS_FILE" > /dev/null 2>&1
cp final_metrology.json run1.json
md5sum run1.json

echo "--- Running Run 2 ---"
./build/tsa "$TS_FILE" > /dev/null 2>&1
cp final_metrology.json run2.json
md5sum run2.json

if diff run1.json run2.json > /dev/null; then
    echo "[PASS] Determinism verified: bit-identical output."
else
    echo "[FAIL] Determinism failure: outputs differ."
    diff -u run1.json run2.json | head -n 20
    exit 1
fi
