#!/bin/bash
# T-STD Long-Term Stability & Metrology Test Script
# Purpose: Drive the purified V3 T-STD engine with carrier-grade parameters.

# Robust ROOT_DIR detection: Resolve 2 levels up from the script's directory
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"

OUT_DIR="${ROOT_DIR}/output"
mkdir -p "$OUT_DIR"

# --- Parameters Configuration ---
# Assuming ffmpeg.wz.master is a sibling directory of tsanalyzer based on current path analysis
FFMPEG_ROOT="$(cd "$ROOT_DIR/../ffmpeg.wz.master" && pwd)"
ffm="${FFMPEG_ROOT}/ffdeps_img/ffmpeg/bin/ffmpeg"
src="/home/lmwang/dev/cae/sample/af2_srt_src.ts"
src="/home/lmwang/dev/cae/sample/af2_srt_src.ts"
prog_id=1
bitrate="1600k"
bitrate_kb=1600
muxrate=2000000
dst="${OUT_DIR}/tstd_smoke.ts"
log_file="${OUT_DIR}/tstd_smoke.log"
test_duration=30

GLOBAL_FAIL=0
echo "[*] Starting T-STD Smoke Test..."
echo "[*] Output directory: $OUT_DIR"
echo "[*] Log file: $log_file"

# --- FFmpeg Command Logic ---
# Optimized with libwz264 CBR settings and T-STD V3 Pacer
cmd="$ffm -y -v trace -i '$src' \
      -t $test_duration \
      -map 0:v:0 -map 0:a:0 \
      -c:a libfdk_aac -profile:a aac_low -b:a 128k \
      -vcodec libwz264 \
      -wz264-params bframes=0:keyint=25:vbv-maxrate=$bitrate:vbv-bufsize=$bitrate:nal-hrd=cbr:force-cfr=1:aud=1 -threads 4 \
      -b:v $bitrate -profile:v Main -preset medium -pix_fmt yuv420p \
      -force_key_frames 'expr:gte(t,n_forced*1)' \
      -dn \
      -flush_packets 0 \
      -metadata service_name="wz_tstd" \
      -metadata service_provider="wz" \
      -f mpegts -mpegts_flags +pat_pmt_at_frames \
      -muxrate $muxrate -muxdelay 0.9 \
      -pcr_period 30 -pat_period 0.1 -sdt_period 0.25 \
      -mpegts_tstd_mode 1 \
      '$dst' \
      > $log_file 2>&1"

# --- Phase 1: High-Precision Metrology Audit ---
echo "================================================"
echo "   PHASE 1: High-Precision Metrology Audit"
echo "================================================"
eval $cmd

if [ $? -eq 0 ]; then
    echo "[SUCCESS] Metrology test completed. Verifying compliance..."
    ${ROOT_DIR}/scripts/ffmpeg_tstd/ffmpeg_tstd_verify_compliance.sh "$log_file"
    RET=$?
    if [ $RET -ne 0 ]; then
        echo -e "\033[33m[WARN] Metrology Audit FAILED. Continuing to stability tests...\033[0m"
        GLOBAL_FAIL=1
    fi
else
    echo "[ERROR] Metrology test crashed."
    exit 1
fi

# --- Phase 2: Comparative Stress Test (Mode 1 vs Mode 2) ---
echo ""
echo "================================================"
echo "   PHASE 2: Comparative Stress Test (30s Parallel)"
echo "================================================"
STABLE_RUNNER="${ROOT_DIR}/scripts/ffmpeg_tstd/tstd_parallel_audit.sh"
if [ -f "$STABLE_RUNNER" ]; then
    chmod +x "$STABLE_RUNNER"
    $STABLE_RUNNER
    RET=$?
    if [ $RET -ne 0 ]; then
        echo "[CRITICAL] Comparative Stress Test FAILED!"
        GLOBAL_FAIL=1
    fi
    echo "[PASS] Comparative Stress Test successful."
else
    echo "[WARN] Parallel audit script not found, skipping Phase 2."
fi

# --- Phase 3: Starvation & Skew Recovery Audit ---
echo ""
echo "================================================"
echo "   PHASE 3: Starvation & Skew Recovery Audit"
echo "================================================"
RECOVERY_RUNNER="${ROOT_DIR}/scripts/ffmpeg_tstd/test_tstd_starvation_recovery.sh"
if [ -f "$RECOVERY_RUNNER" ]; then
    chmod +x "$RECOVERY_RUNNER"
    $RECOVERY_RUNNER
    RET=$?
    if [ $RET -ne 0 ]; then
        echo "[CRITICAL] Starvation Recovery Test FAILED!"
        GLOBAL_FAIL=1
    fi
else
    echo "[WARN] Starvation recovery script not found, skipping Phase 3."
fi

echo ""
echo "------------------------------------------------"
if [ $GLOBAL_FAIL -eq 0 ]; then echo -e "\033[32mSTATUS: ALL REGRESSION PHASES PASSED (GOLDEN)\033[0m"; else echo -e "\033[31mSTATUS: REGRESSION TEST FAILED. REVIEW WARNINGS/ERRORS ABOVE.\033[0m"; fi
echo "------------------------------------------------"
echo "[*] Smoke Test finished."
exit $GLOBAL_FAIL
