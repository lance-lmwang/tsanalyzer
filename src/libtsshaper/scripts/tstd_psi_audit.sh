#!/bin/bash
# T-STD PSI Compliance Audit
# Verifies that PAT/PMT < 500ms and SDT < 2000ms using tsanalyze

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/../../.." && pwd)"
OUT_TS="${ROOT_DIR}/output/psi_audit_test.ts"

# The ffmpeg binary is in the adjacent directory
FFMPEG="${ROOT_DIR}/../ffmpeg.wz.master/ffdeps_img/ffmpeg/bin/ffmpeg"
SAMPLE="${ROOT_DIR}/../sample/HD720p_4Mbps.ts"

if [ ! -f "$FFMPEG" ]; then
    echo "[FAIL] ffmpeg binary not found."
    exit 1
fi

if [ ! -f "$SAMPLE" ]; then
    echo "[WARN] Sample not found. Skipping."
    exit 0
fi

echo "[*] Generating 15s test stream for PSI interval audit..."
$FFMPEG -y -hide_banner -t 15 -i "$SAMPLE" -c copy -muxrate 1700k \
        -pat_period 0.2 -sdt_period 0.25 -mpegts_tstd_mode 1 "$OUT_TS" > /dev/null 2>&1

if [ ! -f "$OUT_TS" ]; then
    echo "[FAIL] Failed to generate test stream."
    exit 1
fi

if ! command -v tsanalyze &> /dev/null; then
    echo "[WARN] tsanalyze tool not found. Please install TSDuck."
    exit 0
fi

echo "[*] Running TSDuck tsanalyze on output stream..."
TSANALYZE_OUT=$(tsanalyze "$OUT_TS" 2>/dev/null)

# Robust Numeric Extraction using Colon Delimiter and Digit Filtering
PAT_MAX=$(echo "$TSANALYZE_OUT" | grep -A 10 "PID: 0x0000 (0)" | grep "Max repet.:" | awk -F: '{print $2}' | tr -dc '0-9')
PMT_MAX=$(echo "$TSANALYZE_OUT" | grep -A 10 "PID: 0x1000 (4096)" | grep "Max repet.:" | awk -F: '{print $2}' | tr -dc '0-9')
SDT_MAX=$(echo "$TSANALYZE_OUT" | grep -A 10 "PID: 0x0011 (17)" | grep "Max repet.:" | awk -F: '{print $2}' | tr -dc '0-9')


FAIL=0

echo "----------------------------------------------------------"
echo "  PSI INTERVAL REPORT (Threshold: PAT/PMT < 500ms, SDT < 2000ms)"
echo "----------------------------------------------------------"

if [ -z "$PAT_MAX" ]; then
    echo "[FAIL] PAT not found!"
    FAIL=1
elif [ "$PAT_MAX" -gt 500 ]; then
    echo "[FAIL] PAT Max Interval = ${PAT_MAX} ms (> 500ms)"
    FAIL=1
else
    echo "[PASS] PAT Max Interval = ${PAT_MAX} ms"
fi

if [ -z "$PMT_MAX" ]; then
    echo "[FAIL] PMT not found!"
    FAIL=1
elif [ "$PMT_MAX" -gt 500 ]; then
    echo "[FAIL] PMT Max Interval = ${PMT_MAX} ms (> 500ms)"
    FAIL=1
else
    echo "[PASS] PMT Max Interval = ${PMT_MAX} ms"
fi

if [ -z "$SDT_MAX" ]; then
    echo "[FAIL] SDT not found!"
    FAIL=1
elif [ "$SDT_MAX" -gt 2000 ]; then
    echo "[FAIL] SDT Max Interval = ${SDT_MAX} ms (> 2000ms)"
    FAIL=1
else
    echo "[PASS] SDT Max Interval = ${SDT_MAX} ms"
fi

echo "----------------------------------------------------------"
exit $FAIL
