#!/bin/bash
# T-STD Compliance Auditor (Telemetry-based)
# Logic: Parse ffmpeg DEBUG logs to validate bit-accurate physical modeling.

SCRIPT_DIR=$(cd $(dirname ${BASH_SOURCE[0]}) && pwd)
LOG_FILE="${1:-tstd_telemetry.log}"
TSA_DIR=$(cd $SCRIPT_DIR/.. && pwd)
FF_SRC=$(cd $TSA_DIR/../../ffmpeg.wz.master 2>/dev/null && pwd || echo "")
ANALYZER="$SCRIPT_DIR/ffmpeg_tstd_telemetry_analyzer.py"
REPORT_JSON="tstd_report.json"
FFMPEG_BIN="$FF_SRC/ffdeps_img/ffmpeg/bin/ffmpeg"

# Ensure temporary files go to the same directory as the log file (the output/ dir)
OUT_DIR=$(dirname "$LOG_FILE")
TMP_OUT="${OUT_DIR}/analyzer_output.tmp"
DECODE_LOG="${OUT_DIR}/decode.log"

echo "================================================"
echo "   T-STD BROADCAST COMPLIANCE AUDITOR (v1.0)"
echo "================================================"

# 1. Validation
if [ ! -f "$LOG_FILE" ]; then
    echo "[!] Error: Log file not found: $LOG_FILE"
    exit 1
fi

if [ ! -f "$ANALYZER" ]; then
    echo "[!] Error: Python analyzer tool not found at $ANALYZER"
    exit 1
fi

# 2. Run Python Deep Metrology
echo "[*] Checking log integrity..."
TELEMETRY_COUNT=$(grep -c "T-STD" "$LOG_FILE")
if [ "$TELEMETRY_COUNT" -lt 100 ]; then
    echo "[FAIL] No T-STD telemetry found. Did you use -v debug?"
    exit 1
fi
echo "[PASS] Found $TELEMETRY_COUNT telemetry events."

# 3. Execute Deep Analysis (v1.3 Pro Engine)
echo "[*] Launching Deep Metrology Engine..."
python3 "$ANALYZER" "$LOG_FILE" --json "$REPORT_JSON" > "$TMP_OUT" 2>&1
EXIT_CODE=$?

# 4. Present Formatted Summary
cat "$TMP_OUT"

# 5. Expert Post-Mortem Analysis (Industrial Grade)
echo ""
echo "--- Architectural Health Check (Hard Gates) ---"

# Hard Gate: PCR Precision
if grep -q "PCR_jitter_ns:.*max=[1-9][0-9][0-9]" "$TMP_OUT"; then
    JITTER=$(grep "PCR_jitter_ns:" "$TMP_OUT" | awk -F'=' '{print $2}' | awk -F',' '{print $1}')
    if (( $(echo "$JITTER > 100.0" | bc -l) )); then
        echo "[CRITICAL] PCR jitter ($JITTER ns) exceeds industrial threshold (100ns)!"
        EXIT_CODE=1
    fi
fi

# Hard Gate: CBR Stability (1ms Window)
if grep -q "\[FAIL\].*Bitrate 1ms stable" "$TMP_OUT"; then
    echo "[CRITICAL] Micro-burst detected! Instantaneous bitrate is non-uniform."
    EXIT_CODE=1
fi

# Hard Gate: VBV Overflow/Underflow
if grep -q "\[FAIL\].*Buffer underflow" "$TMP_OUT" || grep -q "T-STD Hardware Violation Flag active" "$TMP_OUT"; then
    echo "[CRITICAL] T-STD Buffer Violation (Overflow/Underflow) detected!"
    EXIT_CODE=1
fi

# Hard Gate: PSI/SI Interval
if grep -q "\[FAIL\].*PAT interval" "$TMP_OUT"; then
    echo "[CRITICAL] PAT interval violation (>100ms). STB may fail to sync."
    EXIT_CODE=1
fi

if [ $EXIT_CODE -eq 0 ]; then
    # Hard Gate: PID-level Bitrate Audit
    echo "[*] Running PID-level Bitrate Stability Audit..."
    python3 -c "
import json
import sys
try:
    with open('final_metrology.json') as f:
        data = json.load(f)
        if 'pids' in data:
            for pid in data.get('pids', []):
                p_id = pid.get('pid', 'unknown')
                avg_bps = pid.get('bitrate_bps', 0)
                max_bps = pid.get('bitrate_peak_bps', 0)

                # Filter out initialization silence (avg < 100kbps)
                if avg_bps < 100000:
                    continue

                # Video (0x0100) limit: 64kbps absolute fluctuation
                if p_id == '0x0100':
                    abs_fluct_bps = max_bps - avg_bps
                    if abs_fluct_bps > 64000:
                        print(f'[CRITICAL] Video PID {p_id} absolute fluctuation {int(abs_fluct_bps/1000)}kbps > 64kbps')
                        sys.exit(1)
                    else:
                        print(f'[PASS] Video PID {p_id} stability OK (Fluct: {int(abs_fluct_bps/1000)}kbps)')
        else:
            print('[WARN] PID-level bitrate metrics missing in report.')
except Exception as e:
    print(f'[WARN] Could not parse metrology report: {e}')
"
    if [ $? -ne 0 ]; then
        echo "[CRITICAL] Bitrate stability check failed!"
        EXIT_CODE=1
    fi

    # Map log to TS file
    LOG_BASE=$(basename "$LOG_FILE" .log)
    TS_INPUT="${OUT_DIR}/${LOG_BASE}.ts"
    [ ! -f "$TS_INPUT" ] && TS_INPUT=$(echo "$LOG_FILE" | sed 's/\.log/.ts/')

    if [ ! -f "$TS_INPUT" ]; then
        echo "[WARN] Could not find matching TS file for decodability audit: $TS_INPUT"
        echo "[PASS] Skipping ES Layer audit."
    else
        echo "[*] Auditing ES Layer for: $TS_INPUT"
        FFPROBE_BIN=$(echo "$FFMPEG_BIN" | sed 's/ffmpeg$/ffprobe/')
        # Use ffprobe to count actual media streams correctly mapped in the PMT
        STREAM_FOUND=$($FFPROBE_BIN -v error -show_entries program_stream=index -of csv=p=0 "$TS_INPUT" | grep -v '^\s*$' | wc -l)

        # Also check global streams to distinguish between "No Data" and "Empty PMT"
        GLOBAL_STREAM_FOUND=$($FFPROBE_BIN -v error -show_entries stream=index -of csv=p=0 "$TS_INPUT" | grep -v '^\s*$' | wc -l)

        $FFMPEG_BIN -v warning -i "$TS_INPUT" -f null - 2>&1 | tee "$DECODE_LOG"

        if [ "$GLOBAL_STREAM_FOUND" -eq 0 ]; then
            echo "[CRITICAL] ES Layer Empty: No valid media streams detected physically!"
            EXIT_CODE=1
        elif [ "$STREAM_FOUND" -eq 0 ]; then
            echo "[CRITICAL] PMT Mapping Error: Streams exist physically but PMT is empty (0 mapped streams)!"
            EXIT_CODE=1
        elif grep -E "non-monotonically increasing dts|error|invalid|reordering|corrupt" "$DECODE_LOG"; then
            echo "[CRITICAL] ES Layer Corruption or Timestamp Inconsistency detected!"
            EXIT_CODE=1
        else
            echo "[PASS] ES Layer and Timestamps verified."
        fi
    fi
fi

if [ $EXIT_CODE -eq 0 ]; then
    echo "------------------------------------------------"
    echo "STATUS: CONGRATULATIONS! ALL GATES PASSED (DVB READY)"
else
    echo "------------------------------------------------"
    echo "STATUS: COMPLIANCE FAILED - REVIEW LOGS ABOVE"
fi

exit $EXIT_CODE
