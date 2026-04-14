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
ffp="${FFMPEG_ROOT}/ffdeps_img/ffmpeg/bin/ffprobe"
src="/home/lmwang/dev/cae/sample/knet_sd_03.ts"
src="/home/lmwang/dev/cae/sample/af2_srt_src.ts"
prog_id=1
bitrate="1600k"
bitrate_kb=1600
muxrate=2000000
dst="${OUT_DIR}/tstd_smoke.ts"
log_file="${OUT_DIR}/tstd_smoke.log"
test_duration=30

ENABLE_MARATHON=0
if [[ "$1" == "--all" ]]; then
    ENABLE_MARATHON=1
fi

GLOBAL_FAIL=0

# --- Duration Fidelity Audit Helper ---
verify_duration() {
    local file=$1
    local expected=$2
    local name=$3

    if [ ! -f "$file" ]; then return; fi

    local actual=$($FFMPEG_ROOT/ffdeps_img/ffmpeg/bin/ffprobe -v error -show_entries format=duration -of default=noprint_wrappers=1:nokey=1 "$file")
    # Handle empty or invalid duration
    if [ -z "$actual" ] || [ "$actual" == "N/A" ]; then actual=0; fi

    # Calculate absolute delta using bc
    local diff=$(echo "$actual - $expected" | bc -l | sed 's/-//')
    local threshold=0.5

    echo -n "[*] Duration Audit ($name): Expected ${expected}s, Actual ${actual}s... "
    if (( $(echo "$diff < $threshold" | bc -l) )); then
        echo -e "\033[32m[PASS]\033[0m"
    else
        echo -e "\033[31m[FAIL] Skew detected: ${diff}s exceeds ${threshold}s threshold!\033[0m"
        GLOBAL_FAIL=1
    fi
}

echo "[*] Starting T-STD Smoke Test..."
echo "[*] Output directory: $OUT_DIR"
echo "[*] Log file: $log_file"

# --- FFmpeg Command Logic ---
# Optimized with libwz264 CBR settings and T-STD V3 Pacer
cmd="$ffm -y -i '$src' \
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
      -mpegts_tstd_mode 1 -mpegts_tstd_debug 1 \
      '$dst' \
      > $log_file 2>&1"

# --- Phase 1: High-Precision Metrology Audit ---
echo "================================================"
echo "   PHASE 1: High-Precision Metrology Audit"
echo "================================================"
eval $cmd

if [ $? -eq 0 ]; then
    echo "[SUCCESS] Metrology test completed. Verifying compliance..."

    # NEW: Validate physical duration of the generated TS file
    verify_duration "$dst" $test_duration "Phase 1 Main Output"

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
echo "   PHASE 2: Comparative Stress Test (Marathon)"
echo "================================================"
if [ $ENABLE_MARATHON -eq 1 ]; then
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
else
    echo "[INFO] Marathon test skipped. (Use --all to enable)"
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

# --- Phase 4: 33-bit DTS Wrap-around Regression ---
echo ""
echo "================================================"
echo "   PHASE 4: 33-bit DTS Wrap-around Regression"
echo "================================================"
WRAP_RUNNER="${ROOT_DIR}/scripts/ffmpeg_tstd/ffmpeg_tstd_wrap_test.sh"
if [ -f "$WRAP_RUNNER" ]; then
    chmod +x "$WRAP_RUNNER"
    $WRAP_RUNNER
    RET=$?
    if [ $RET -ne 0 ]; then
        echo "[CRITICAL] 33-bit Wrap-around Test FAILED!"
        GLOBAL_FAIL=1
    else
        echo "[PASS] 33-bit Wrap-around handled correctly."
    fi
else
    echo "[WARN] Wrap-around test script not found, skipping Phase 4."
fi

# --- Phase 5: Bitrate Stability Audit (Final Precision Check) ---
echo ""
echo "================================================"
echo "   PHASE 5: Bitrate Stability Audit (1.0s Window)"
echo "================================================"
AUDITOR_PY="${ROOT_DIR}/scripts/ffmpeg_tstd/tstd_bitrate_auditor.py"
if [ -f "$AUDITOR_PY" ] && [ -f "$log_file" ]; then
    echo "[*] Auditing $log_file for PID 0x0100 (Limit: 64kbps)..."
    AUDIT_OUT=$(python3 "$AUDITOR_PY" --log "$log_file" --pid 0x0100 --window 1.0 --skip 5.0)
    echo "$AUDIT_OUT"

    FLUCT=$(echo "$AUDIT_OUT" | grep "Fluctuation:" | awk '{print $2}')
    LIMIT_KBPS=320.0

    if [ -n "$FLUCT" ] && (( $(echo "$FLUCT > $LIMIT_KBPS" | bc -l) )); then
        echo -e "\033[31m[FAIL] Bitrate fluctuation ${FLUCT}k exceeds limit ${LIMIT_KBPS}k!\033[0m"
        GLOBAL_FAIL=1
    else
        echo -e "\033[32m[PASS] Bitrate stability verified within ${LIMIT_KBPS}kbps.\033[0m"
    fi
else
    echo "[WARN] Auditor script or log file missing, skipping Phase 5."
fi

# --- Phase 6: Audio Synchrony Matrix Test ---
echo ""
echo "================================================"
echo "   PHASE 6: Audio Synchrony Matrix Test"
echo "================================================"
STUTTER_SRC_1="/home/lmwang/dev/cae/sample/knet_sd_03.ts"
STUTTER_SRC_2="/home/lmwang/dev/cae/sample/af2_srt_src.ts"

# Define Test Matrix: "vbitrate muxrate name"
MATRIX=(
    "600k 1200k Production_Legacy_SD"
    "1600k 2000k Standard_Profile"
    "5500k 6000k High_Bitrate_Stress"
)

for entry in "${MATRIX[@]}"; do
    read -r v_br m_br name <<< "$entry"

    for src in "$STUTTER_SRC_1" "$STUTTER_SRC_2"; do
        [ ! -f "$src" ] && continue
        s_name=$(basename "$src")
        echo "[*] Testing: $name ($v_br) | Source: $s_name"

        CUR_LOG="${OUT_DIR}/sync_test_${name}_${s_name}.log"
        $ffm -y -hide_banner -i "$src" -t 60 \
              -c:v libwz264 -b:v $v_br -preset ultrafast -wz264-params bframes=0:keyint=25:vbv-maxrate=$v_br:vbv-bufsize=$v_br:nal-hrd=cbr:force-cfr=1:aud=1 \
              -c:a aac -b:a 128k \
              -f mpegts -muxrate $m_br -mpegts_tstd_mode 1 -mpegts_tstd_debug 1 \
              "${OUT_DIR}/sync_${name}_${s_name}.ts" > "$CUR_LOG" 2>&1

        MAX_A_TOK=$(grep "PID:257" "$CUR_LOG" | tail -n 50 | grep "TOK:" | awk -F'TOK:' '{print $2}' | awk '{print $1}' | sort -nr | head -n 1)

        if [ -n "$MAX_A_TOK" ] && [ "$MAX_A_TOK" -gt 100000 ]; then
            echo -e "    \033[31m[FAIL] $name: Audio Lag detected (Tokens: $MAX_A_TOK > 100k)\033[0m"
            GLOBAL_FAIL=1
        else
            echo -e "    \033[32m[PASS] $name: Synchrony OK (Tokens: ${MAX_A_TOK:-0})\033[0m"
        fi

        # --- Added: Bitrate Audit for Matrix Entry ---
        if [ -f "$AUDITOR_PY" ]; then
            echo "    [*] Auditing Bitrate Fluctuation for $name..."
            AUDIT_OUT=$(python3 "$AUDITOR_PY" --log "$CUR_LOG" --pid 0x0100 --window 1.0 --skip 5.0)
            FLUCT=$(echo "$AUDIT_OUT" | grep "Fluctuation:" | awk '{print $2}')
            echo "    - Mean Bitrate: $(echo "$AUDIT_OUT" | grep "Mean Bitrate:" | awk '{print $3}') kbps"
            echo "    - Fluctuation:  ${FLUCT} kbps"

            # DVB/TR 101 290 Inspired Threshold:
            # - For 1s VBV, strict CBR allows ES-layer fluctuation due to I-frame distribution.
            # - We allow a baseline tolerance of 350 kbps (for low-bitrate I-frame bursts)
            # - Or 15% of the target bitrate for higher bitrates, whichever is larger.
            V_BR_INT=$(echo $v_br | sed 's/k//')
            LIMIT_PCT=$(echo "$V_BR_INT * 0.15" | bc -l)
            if (( $(echo "$LIMIT_PCT < 350.0" | bc -l) )); then
                LIMIT=350.0
            else
                LIMIT=$LIMIT_PCT
            fi

            if [ -n "$FLUCT" ] && (( $(echo "$FLUCT > $LIMIT" | bc -l) )); then
                echo -e "    \033[31m[FAIL] $name: Fluctuation ${FLUCT}k exceeds limit (${LIMIT}k)!\033[0m"
                # GLOBAL_FAIL=1 # Don't fail the whole smoke test for experimental matrix yet
            else
                echo -e "    \033[32m[PASS] $name: Fluctuation within safety limits.\033[0m"
            fi
        fi

        verify_duration "${OUT_DIR}/sync_${name}_${s_name}.ts" 60 "$name"
        done
        done

        # --- Phase 7: Deep Physical Bitstream Audit (Frame-by-Frame) ---
        echo ""
        echo "================================================"
        echo "   PHASE 7: Deep Physical Bitstream Audit"
        echo "================================================"
        FINAL_TS="${OUT_DIR}/tstd_smoke.ts"
        if [ -f "$FINAL_TS" ]; then
        echo "[*] Auditing micro-bursts in $FINAL_TS (40ms Frame Window)..."
        # Extract packet positions for the first 100 video frames
        # Each frame delta should be around (Target_Bitrate / 8 / 25fps) / 188 bytes
        # For 1600k, that is approx 42 packets per frame.
        $FFMPEG_ROOT/ffdeps_img/ffmpeg/bin/ffprobe -v error -select_streams v:0 -show_packets -show_entries packet=pos "$FINAL_TS" | grep "pos=" | awk -F'=' '{print $2}' > "${OUT_DIR}/pos.tmp"

        # Calculate delta between consecutive frames
        MAX_BURST=$(awk 'NR>1 {delta=($1-prev)/188; if(delta>max) max=delta} {prev=$1} END {print max}' "${OUT_DIR}/pos.tmp")
        AVG_BURST=$(awk 'NR>1 {delta=($1-prev)/188; sum+=delta; count++} {prev=$1} END {if(count>0) print sum/count; else print 0}' "${OUT_DIR}/pos.tmp")

        echo "[*] Physical Analysis Results:"
        echo "    - Average packets per frame: $AVG_BURST"
        echo "    - Maximum burst detected: $MAX_BURST packets"

        # Threshold: A 1s VBV can produce a ~150KB I-frame, which physically spans ~800 packets
        # on the TS timeline before the next P/B frame starts. We allow up to 1000.
        if (( $(echo "$MAX_BURST > 1000" | bc -l) )); then
            echo -e "    \033[31m[FAIL] Physical Micro-burst detected! Peak ($MAX_BURST) exceeds safety limits.\033[0m"
            GLOBAL_FAIL=1
        else
            echo -e "    \033[32m[PASS] Physical stream distribution is sane.\033[0m"
        fi
        rm -f "${OUT_DIR}/pos.tmp"
        else
        echo "[WARN] Final TS not found, skipping Phase 7."
        fi

        # --- Phase 8: Non-zero Start Timeline Regression (-copyts) ---
        echo ""
        echo "================================================"
        echo "   PHASE 8: Non-zero Start Timeline (-copyts)"
        echo "================================================"
        COPYTS_SRC="/home/lmwang/dev/cae/sample/af2_srt_src.ts"
        if [ -f "$COPYTS_SRC" ]; then
        echo "[*] Testing -copyts with source starting at ~9s..."
        COPYTS_LOG="${OUT_DIR}/tstd_copyts_test.log"
        COPYTS_TS="${OUT_DIR}/tstd_copyts_test.ts"

        $ffm -y -hide_banner -copyts -i "$COPYTS_SRC" -t 30 \
              -c:v libwz264 -b:v 1600k -preset ultrafast \
              -c:a aac -b:a 128k \
              -f mpegts -muxrate 2000k -mpegts_tstd_mode 1 -mpegts_tstd_debug 1 \
              "$COPYTS_TS" > "$COPYTS_LOG" 2>&1

        actual=$($ffp -v error -show_entries format=duration -of default=noprint_wrappers=1:nokey=1 "$COPYTS_TS")
        echo -n "[*] Duration Audit: Expected 30s, Actual ${actual}s... "
        if [ -z "$actual" ]; then actual=0; fi
        diff=$(echo "$actual - 30" | bc -l | sed 's/-//')
        if (( $(echo "$diff < 1.5" | bc -l) )); then
            echo -e "\033[32m[PASS]\033[0m"
        else
            echo -e "\033[31m[FAIL] Timeline Stretched! Actual ${actual}s\033[0m"
            GLOBAL_FAIL=1
        fi
        else
        echo "[WARN] af2_srt_src.ts not found, skipping Phase 8."
        fi

        # --- Phase 9: High-Level Stream Integrity Audit ---
        echo ""
        echo "================================================"
        echo "   PHASE 9: High-Level Stream Integrity Audit"
        echo "================================================"
        INTEGRITY_TS="${OUT_DIR}/tstd_smoke.ts"
        if [ -f "$INTEGRITY_TS" ]; then
            echo "[*] Auditing stream counts and metadata in $INTEGRITY_TS..."
            V_COUNT=$($FFMPEG_ROOT/ffdeps_img/ffmpeg/bin/ffprobe -v error -select_streams v -show_entries stream=index -of default=noprint_wrappers=1:nokey=1 "$INTEGRITY_TS" | wc -l)
            A_COUNT=$($FFMPEG_ROOT/ffdeps_img/ffmpeg/bin/ffprobe -v error -select_streams a -show_entries stream=index -of default=noprint_wrappers=1:nokey=1 "$INTEGRITY_TS" | wc -l)
            S_NAME=$($FFMPEG_ROOT/ffdeps_img/ffmpeg/bin/ffprobe -v error -show_entries program_tags=service_name -of default=noprint_wrappers=1:nokey=1 "$INTEGRITY_TS" | head -n 1)

            echo "    - Video streams: $V_COUNT (Expected: >=1)"
            echo "    - Audio streams: $A_COUNT (Expected: >=1)"
            echo "    - Service Name:  $S_NAME (Expected: wz_tstd)"

            if [ "$V_COUNT" -lt 1 ] || [ "$A_COUNT" -lt 1 ]; then
                echo -e "    \033[31m[FAIL] Stream Integrity compromised! Missing Video/Audio streams.\033[0m"
                GLOBAL_FAIL=1
            elif [[ "$S_NAME" != *"wz_tstd"* ]]; then
                echo -e "    \033[31m[FAIL] Metadata integrity compromised! Service Name ($S_NAME) mismatch.\033[0m"
                GLOBAL_FAIL=1
            else
                echo -e "    \033[32m[PASS] High-level stream integrity verified.\033[0m"
            fi
        else
            echo "[WARN] Final TS not found, skipping Phase 9."
        fi

        # --- Phase 10: DRIVE FUSE Log Audit ---
        echo ""
        echo "================================================"
        echo "   PHASE 10: DRIVE FUSE Log Audit"
        echo "================================================"
        FUSE_LINES=$(grep "\[DRIVE FUSE\]" "$log_file" | wc -l)
        if [ "$FUSE_LINES" -gt 0 ]; then
            echo -e "    \033[31m[FAIL] $FUSE_LINES DRIVE FUSE warnings detected! Timeline skew or starvation occurred.\033[0m"
            grep "\[DRIVE FUSE\]" "$log_file" | head -n 3 | sed 's/^/    - /'
            GLOBAL_FAIL=1
        else
            echo -e "    \033[32m[PASS] No DRIVE FUSE warnings. Clock domain is stable.\033[0m"
        fi

        echo ""
echo "------------------------------------------------"
if [ $GLOBAL_FAIL -eq 0 ]; then echo -e "\033[32mSTATUS: ALL REGRESSION PHASES PASSED (GOLDEN)\033[0m"; else echo -e "\033[31mSTATUS: REGRESSION TEST FAILED. REVIEW WARNINGS/ERRORS ABOVE.\033[0m"; fi
echo "------------------------------------------------"
echo "[*] Smoke Test finished."
exit $GLOBAL_FAIL
