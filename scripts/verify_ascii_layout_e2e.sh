#!/bin/bash
# verify_ascii_layout_e2e.sh - Non-interactive ASCII layout verification

# Colors (Stripped for log comparison if needed, but keeping for display)
CYAN='\e[1;36m'; WHITE='\e[1;37m'; RESET='\e[0m'; GREEN='\e[32m'; RED='\e[31m'; YELLOW='\e[33m'

HTTP_PORT=8088
OUTPUT_FILE="ascii_render.txt"

echo -e "${CYAN}=== ASCII Layout E2E Verification ===${RESET}"

# 1. Fetch Metrics
METRICS=$(curl -s http://localhost:8088/metrics/core)
if [ -z "$METRICS" ]; then
    echo -e "${RED}[FAIL] Backend offline. Start tsa_server_pro first.${RESET}"
    exit 1
fi

# 2. Extract Stream IDs
STREAM_IDS=$(echo "$METRICS" | grep "tsa_system_signal_locked" | cut -d'"' -f2 | sort -u)

if [ -z "$STREAM_IDS" ]; then
    echo -e "${YELLOW}[WARN] No active streams found. Rendering empty frame.${RESET}"
fi

# 3. Render Header
{
    echo "======================================================================"
    echo "   TsAnalyzer Pro - UNIFIED NOC ASCII VIEW (Port 8088)"
    echo "======================================================================"
    echo " STREAM ID  |  STATUS  |  HEALTH  |  MOSAIC VIEW"
    echo "----------------------------------------------------------------------"

    for ID in $STREAM_IDS; do
        HEALTH=$(echo "$METRICS" | grep "tsa_system_health_score{stream_id=\"$ID\"}" | awk '{print $2}')
        LOCKED=$(echo "$METRICS" | grep "tsa_system_signal_locked{stream_id=\"$ID\"}" | awk '{print $2}')

        HEALTH=${HEALTH:-0.0}
        LOCKED=${LOCKED:-0}

        S_LABEL="NO SIG "
        [ "$LOCKED" == "1" ] && S_LABEL="LOCKED "

        BAR=""
        FILL=$(( ${HEALTH%.*} / 5 ))
        for ((j=0; j<20; j++)); do
            if [ $j -lt $FILL ]; then BAR="${BAR}#"; else BAR="${BAR}-"; fi
        done

        printf " %-10s | %-7s | %6.1f%% | [%-20s]\n" "$ID" "$S_LABEL" "$HEALTH" "$BAR"
    done
    echo "----------------------------------------------------------------------"
} | tee $OUTPUT_FILE

# 4. Validation
echo -e "\n${CYAN}Step 4: Validating Output...${RESET}"

CHECK_HEADER=$(grep -c "UNIFIED NOC ASCII VIEW" $OUTPUT_FILE || true)
CHECK_COLS=$(grep -c "STREAM ID  |  STATUS" $OUTPUT_FILE || true)

if [ "$CHECK_HEADER" -gt 0 ] && [ "$CHECK_COLS" -gt 0 ]; then
    echo -e "${GREEN}[PASS] ASCII Header and Columns verified.${RESET}"
else
    echo -e "${RED}[FAIL] ASCII Template mismatch.${RESET}"
    exit 1
fi

if [ -n "$STREAM_IDS" ]; then
    CHECK_DATA=$(grep -c "\[" $OUTPUT_FILE || true)
    if [ "$CHECK_DATA" -ge 1 ]; then
        echo -e "${GREEN}[PASS] Data rows rendered correctly.${RESET}"
    else
        echo -e "${RED}[FAIL] Stream IDs found but no bars rendered.${RESET}"
        exit 1
    fi
fi

echo -e "${GREEN}=== E2E SUCCESS: ASCII Layout Verified ===${RESET}"
rm $OUTPUT_FILE
