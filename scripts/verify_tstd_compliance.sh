#!/bin/bash
# T-STD Pro Compliance Auditor
# Specifically designed to verify Broadcast-Grade Mux Core standards.

SCRIPT_DIR=$(cd $(dirname ${BASH_SOURCE[0]}) && pwd)
LOG_FILE="${1:-tstd_telemetry.log}"
TSA_DIR=$(cd $SCRIPT_DIR/.. && pwd)
FF_SRC=$(cd $TSA_DIR/../ffmpeg.wz.master 2>/dev/null && pwd || echo "")
ANALYZER="$SCRIPT_DIR/tstd_telemetry_analyzer.py"
REPORT_JSON="tstd_report.json"
FFMPEG_BIN="$FF_SRC/ffdeps_img/ffmpeg/bin/ffmpeg"

echo "================================================"
echo "   T-STD BROADCAST COMPLIANCE AUDITOR (v1.0)"
echo "================================================"

if [ ! -f "$LOG_FILE" ]; then
    echo "[!] Error: Log file '$LOG_FILE' not found."
    exit 1
fi

if [ ! -f "$ANALYZER" ]; then
    echo "[!] Error: Python analyzer tool not found at $ANALYZER"
    exit 1
fi

# 1. Basic Integrity Check
echo "[*] Checking log integrity..."
TSTD_COUNT=$(grep -c "\[T-STD\]" "$LOG_FILE")
if [ "$TSTD_COUNT" -eq 0 ]; then
    echo "[FAIL] No T-STD telemetry found. Did you use -v debug?"
    exit 1
fi
echo "[PASS] Found $TSTD_COUNT telemetry events."

# 2. Execute Deep Analysis (v1.3 Pro Engine)
echo "[*] Launching Deep Metrology Engine..."
python3 "$ANALYZER" "$LOG_FILE" --json "$REPORT_JSON" > analyzer_output.tmp 2>&1
EXIT_CODE=$?

# 3. Present Formatted Summary
cat analyzer_output.tmp

# 4. Expert Post-Mortem Analysis (Industrial Grade)
echo ""
echo "--- Architectural Health Check (Hard Gates) ---"

# Hard Gate: PCR Precision
if grep -q "PCR_jitter_ns:.*max=[1-9][0-9][0-9]" analyzer_output.tmp; then
    JITTER=$(grep "PCR_jitter_ns:" analyzer_output.tmp | awk -F'=' '{print $2}' | awk -F',' '{print $1}')
    if (( $(echo "$JITTER > 100.0" | bc -l) )); then
        echo "[CRITICAL] PCR jitter ($JITTER ns) exceeds industrial threshold (100ns)!"
        EXIT_CODE=1
    fi
fi

# Hard Gate: CBR Stability (1ms Window)
if grep -q "\[FAIL\].*CBR violation at 1ms" analyzer_output.tmp; then
    echo "[CRITICAL] Micro-burst detected! Instantaneous bitrate is non-uniform."
    EXIT_CODE=1
fi

# Hard Gate: VBV Overflow/Underflow
if grep -q "\[FAIL\].*Buffer underflow" analyzer_output.tmp || grep -q "T-STD Hardware Violation Flag active" analyzer_output.tmp; then
    echo "[CRITICAL] T-STD Buffer Violation (Overflow/Underflow) detected!"
    EXIT_CODE=1
fi

# Hard Gate: PSI/SI Interval
if grep -q "\[FAIL\].*PAT interval" analyzer_output.tmp; then
    echo "[CRITICAL] PAT interval violation (>100ms). STB may fail to sync."
    EXIT_CODE=1
fi

if [ $EXIT_CODE -eq 0 ]; then
    # Final Security Gate: Decodability & Timestamp Audit
    echo "[*] Running final ES Layer & Timestamp Monotonicity audit..."
    # We use -v warning and check for specific broadcast-killing issues
    $FFMPEG_BIN -v warning -i test_bench.ts -f null - 2>&1 | tee decode.log

    if grep -E "non-monotonically increasing dts|error|invalid|reordering|corrupt" decode.log; then
        echo "[CRITICAL] ES Layer Corruption or Timestamp Inconsistency detected!"
        EXIT_CODE=1
    else
        echo "[PASS] ES Layer and Timestamps verified."
    fi
    rm -f decode.log
fi

if [ $EXIT_CODE -eq 0 ]; then
    echo "------------------------------------------------"
    echo "STATUS: CONGRATULATIONS! ALL GATES PASSED (DVB READY)"
else
    echo "------------------------------------------------"
    echo "STATUS: COMPLIANCE FAILED - REVIEW LOGS ABOVE"
fi
echo "================================================"

rm -f analyzer_output.tmp
exit $EXIT_CODE
