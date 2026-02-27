#!/bin/bash

# TsAnalyzer Monitoring Target Remover
# Usage: ./remove-target.sh <IP:PORT>

TARGET_FILE="prometheus/targets.json"

if [ "$#" -lt 1 ]; then
    echo "Usage: $0 <IP:PORT>"
    exit 1
fi

IP_PORT=$1

# Use a temporary file to filter out the target
# This is a very basic implementation.
# In production, using 'jq' is highly recommended for JSON manipulation.
grep -v "$IP_PORT" "$TARGET_FILE" > "${TARGET_FILE}.tmp"
mv "${TARGET_FILE}.tmp" "$TARGET_FILE"

# Basic repair of JSON structure if it gets broken by grep
# Ensure it starts with [ and ends with ]
sed -i '1s/^/[/' "$TARGET_FILE"
echo "]" >> "$TARGET_FILE"
# Remove double brackets if any
sed -i 's/\[\[/\[/g' "$TARGET_FILE"
sed -i 's/\]\]/\]/g' "$TARGET_FILE"

echo "Target $IP_PORT removed (if it existed)."
