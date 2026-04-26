#!/bin/bash
# T-STD Performance Alignment Auditor
# Compares current run metrics against Golden Baseline with strict 5k tolerance.

# Golden Values (Delta in kbps)
GOLDEN_SD=60
GOLDEN_720P=39
GOLDEN_1080I=69
TOLERANCE=5

GLOBAL_PASS=1

check_metric() {
    local name=$1
    local current=$2
    local golden=$3

    local diff=$((current - golden))
    local abs_diff=${diff#-}

    echo -n "Checking $name: Current=${current}k, Golden=${golden}k, Diff=${diff}k... "

    if [ $abs_diff -le $TOLERANCE ]; then
        echo -e "\033[32m[PASS]\033[0m"
    else
        echo -e "\033[31m[FAIL] (Exceeds ${TOLERANCE}k tolerance)\033[0m"
        GLOBAL_PASS=0
    fi
}

# Extract values from output logs
# We assume logs follow the pattern: tstd_<name>_md0.9.v2.log
EXTRACT_CMD="grep 'Bitrate Stats' | sed -n 's/.*Delta: \([0-9]*\)k.*/\1/p'"

CUR_SD=$(cat output/tstd_sd_md0.9.v2.log | eval $EXTRACT_CMD)
CUR_720P=$(cat output/tstd_720p_md0.9.v2.log | eval $EXTRACT_CMD)
CUR_1080I=$(cat output/tstd_1080i_md0.9.v2.log | eval $EXTRACT_CMD)

echo "=========================================================="
echo "   T-STD PERFORMANCE ALIGNMENT REPORT"
echo "=========================================================="
[ -n "$CUR_SD" ] && check_metric "SD   " "$CUR_SD" "$GOLDEN_SD"
[ -n "$CUR_720P" ] && check_metric "720p " "$CUR_720P" "$GOLDEN_720P"
[ -n "$CUR_1080I" ] && check_metric "1080i" "$CUR_1080I" "$GOLDEN_1080I"
echo "=========================================================="

if [ $GLOBAL_PASS -eq 1 ]; then
    echo -e "\033[32mALIGNMENT SUCCESSFUL. NO REGRESSION DETECTED.\033[0m"
    exit 0
else
    echo -e "\033[31mALIGNMENT FAILED! PLEASE REVIEW ALGORITHMIC CHANGES.\033[0m"
    exit 1
fi
