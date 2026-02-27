#!/bin/bash

# TsAnalyzer Monitoring Target Manager
# Usage: ./add-target.sh <IP:PORT> <INSTANCE_NAME>

TARGET_FILE="prometheus/targets.json"

if [ "$#" -lt 2 ]; then
    echo "Usage: $0 <IP:PORT> <INSTANCE_NAME>"
    echo "Example: $0 192.168.1.10:8000 remote-probe-1"
    exit 1
fi

IP_PORT=$1
NAME=$2

# Check if target already exists
if grep -q "$IP_PORT" "$TARGET_FILE"; then
    echo "Target $IP_PORT already exists. Updating label..."
    # Simple replacement if exists
    sed -i "s/"targets": \["$IP_PORT"\].*/"targets": \["$IP_PORT"\], "labels": \{ "instance": "$NAME" \} \},/g" "$TARGET_FILE"
else
    # Append new target before the last closing bracket
    # Note: This uses a simple method, for complex JSON processing 'jq' would be better.
    # But to keep it zero-dependency, we use sed to insert before the last line.
    sed -i "$ s/\]/  , \{ "targets": ["$IP_PORT"], "labels": \{ "instance": "$NAME" \} \}
\]/" "$TARGET_FILE"
    echo "Target $IP_PORT ($NAME) added successfully."
fi
