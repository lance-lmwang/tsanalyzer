#!/bin/bash
# T-STD V3 Physical Alignment Auditor (Zero-Config Analysis Mode)
# Usage: ./tstd_promax_alignment_audit.sh <TS_FILE>
# [INTERNAL DEPENDENCY] Crucial component of tstd_full_validation.sh Stage 8.
# NO LOGS. NO BITRATE SPEC REQUIRED. Direct physical sensing.

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
AUDITOR_PY="${SCRIPT_DIR}/ts_pcr_sliding_window.py"

TS_FILE=$1

if [ ! -f "$TS_FILE" ]; then
    echo "[ERROR] Input file $TS_FILE not found."
    exit 1
fi

# Run Self-Sensing Python Auditor
# Result format: MIN MAX DELTA AVG STD REL_PCT
RESULT=$(python3 "$AUDITOR_PY" "$TS_FILE"  2>/dev/null)

if [ -z "$RESULT" ] || [ "$RESULT" == "0 0 0 0 0 0" ]; then
    echo "[ERROR] Physical auditor failed to sense PCR/Video anchors."
    exit 1
fi

read r_min r_max r_delta r_avg r_std r_pct_raw <<< "$RESULT"

# --- SMART VERDICT LOGIC ---
# Standard 1: Relative Fluctuation (Delta) should be < 5.6% of Muxrate.
# Standard 2: Absolute Fluctuation (Delta) should be < 88k (corresponds to +/- 44k).
THRESHOLD_PCT=5.6
THRESHOLD_ABS=88

# Use provided Muxrate as denominator if available, otherwise fallback to measured average
MUX_REF=$2
if [ ! -z "$MUX_REF" ] && [ "$MUX_REF" -gt 0 ]; then
    r_pct=$(echo "scale=2; $r_delta * 100.0 / $MUX_REF" | bc -l)
    REF_LABEL="Muxrate: ${MUX_REF}k"
else
    r_pct=$r_pct_raw
    REF_LABEL="Measured Avg"
fi

echo "---------------------------------------------------------------------------"
echo "PROMAX PHYSICAL AUDIT (PCR-Windowed - ZeroConfig):"
printf "Bitrate Stats : Min: %dk, Max: %dk, Delta: %dk, Avg: %.1f k\n" \
       "${r_min%.*}" "${r_max%.*}" "${r_delta%.*}" "$r_avg"
printf "Stability     : Relative: %s%% (Ref: %s, Limit: < %s%%), Abs Delta: %dk (Limit: < %dk)\n" \
       "$r_pct" "$REF_LABEL" "$THRESHOLD_PCT" "${r_delta%.*}" "$THRESHOLD_ABS"

if (( $(echo "$r_pct < $THRESHOLD_PCT" | bc -l) )) || (( $(echo "${r_delta%.*} < $THRESHOLD_ABS" | bc -l) )); then
    echo -e "STATUS        : \033[32m[PASS] Physical alignment verified.\033[0m"
    RET=0
else
    echo -e "STATUS        : \033[31m[FAIL] Physical fluctuation too high!\033[0m"
    RET=1
fi
echo "---------------------------------------------------------------------------"

exit $RET
