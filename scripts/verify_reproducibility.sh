#!/bin/bash
# TsAnalyzer: Determinism Verification Suite
# Ensures that two runs on the same input produce bit-identical results.

TS_FILE=$1
if [ -z "$TS_FILE" ]; then
    echo "Usage: $0 <test.ts>"
    exit 1
fi

BIN="./build/tsa"
if [ ! -f "$BIN" ]; then
    echo "Error: Binary $BIN not found. Build the project first."
    exit 1
fi

OUT1="run1.json"
OUT2="run2.json"

echo "=== TsAnalyzer Determinism Test ==="
echo "Running first pass..."
$BIN --mode=replay "$TS_FILE" > /dev/null
mv final_metrology.json "$OUT1"

echo "Running second pass..."
$BIN --mode=replay "$TS_FILE" > /dev/null
mv final_metrology.json "$OUT2"

# Compare results (ignoring engine_latency_ns which can vary)
MD5_1=$(grep -v "engine_latency_ns" "$OUT1" | md5sum | cut -d' ' -f1)
MD5_2=$(grep -v "engine_latency_ns" "$OUT2" | md5sum | cut -d' ' -f1)

echo "Run 1 Hash: $MD5_1"
echo "Run 2 Hash: $MD5_2"

if [ "$MD5_1" == "$MD5_2" ]; then
    echo "SUCCESS: Results are bit-identical (deterministic)."
    rm "$OUT1" "$OUT2"
    exit 0
else
    echo "FAILURE: Non-deterministic results detected!"
    diff -u "$OUT1" "$OUT2" | head -n 20
    exit 1
fi
