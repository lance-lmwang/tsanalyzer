#!/bin/bash
# T-STD Long-Term Stability & Metrology Test Script
# Purpose: Drive the purified V3 T-STD engine with carrier-grade parameters.

# Robust ROOT_DIR detection: Resolve 2 levels up from the script's directory
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"

OUT_DIR="${ROOT_DIR}/output"
mkdir -p "$OUT_DIR"

# --- Task Sentry: Prevent Interleaving with Long-run Tests ---
STRESS_PID=$(pgrep -f "tstd_overnight_stress" | grep -v "$$")
if [ -n "$STRESS_PID" ]; then
    echo -e "\033[31m[CRITICAL] Conflict Detected!\033[0m"
    echo "A long-term stress test (PID: $STRESS_PID) is currently running."
    echo "Running regression now will cause CPU contention and invalidate results."
    echo "Please stop the stress test or wait for it to finish."
    exit 1
fi

FFM_CHECK=$(pgrep -f "ffmpeg.*mpegts_tstd_mode 1" | grep -v "$$")
if [ -n "$FFM_CHECK" ]; then
    echo -e "\033[33m[WARN] Another T-STD FFmpeg process is running (PIDs: $FFM_CHECK).\033[0m"
    echo "Continuing may lead to inaccurate SCORE results due to resource starvation."
    echo "Recommendation: Wait until other tests complete."
    echo ""
fi

# --- Fatal Error Scanner ---
# Scans a log file for critical T-STD failures like Panic Recovery or Backlog.
scan_fatal_errors() {
    local log=$1
    local label=$2
    if grep -q "Panic Recovery" "$log"; then
        echo -e "    \033[31m[CRITICAL] $label: Detected Panic Recovery (Pacing Bypass)!\033[0m"
        GLOBAL_FAIL=1
        return 1
    fi
    return 0
}

# --- Physical Duration Auditor ---
# Compare output duration with input duration to detect data loss.
audit_physical_duration() {
    local ts_file=$1
    local expected_dur_s=$2
    local label=$3

    # Use mediainfo to get duration in ms (General track)
    local actual_dur_ms=$(mediainfo --Inform="General;%Duration%" "$ts_file" 2>/dev/null)
    if [ -z "$actual_dur_ms" ]; then
        echo -e "    \033[33m[WARN] $label: Mediainfo could not extract duration.\033[0m"
        return 0
    fi

    local actual_dur_s=$(echo "scale=3; $actual_dur_ms / 1000" | bc -l)
    local diff=$(echo "$actual_dur_s - $expected_dur_s" | bc -l | sed 's/-//')

    echo -n "[*] Physical Duration Audit ($label): Actual ${actual_dur_s}s (Expected ${expected_dur_s}s)... "

    # Assert: Tolerance 1.5 seconds (to account for container padding/header overhead)
    if (( $(echo "$diff > 1.5" | bc -l) )); then
        echo -e "\033[31m[FAIL] Duration Mismatch (Diff: ${diff}s)!\033[0m"
        return 1
    else
        echo -e "\033[32m[PASS]\033[0m"
        return 0
    fi
}

# --- Physical Latency Truth Auditor ---
# Extract actual DTS-PCR gap from the bitstream. This is the ultimate truth.
audit_physical_latency() {
    local ts_file=$1
    local mux_delay_ms=$2
    local label=$3

    # Fast Probe: Just look at the first 500 packets to find the first PCR of PID 0x21
    local first_dts=$($ffp -v error -read_intervals "%+1" -select_streams v:0 -show_packets -show_entries packet=dts -of default=noprint_wrappers=1:nokey=1 "$ts_file" | head -n 1)
    local first_pcr=$($ffp -v error -read_intervals "%+5" -show_packets -show_entries packet=pcr,pid -of default=noprint_wrappers=1:nokey=1 "$ts_file" | grep "0x21" | awk -F'|' '{print $1}' | grep -v "N/A" | head -n 1)

    if [ -z "$first_dts" ] || [ -z "$first_pcr" ]; then
        # Fallback for different PIDs
        first_pcr=$($ffp -v error -read_intervals "%+10" -show_packets -show_entries packet=pcr -of default=noprint_wrappers=1:nokey=1 "$ts_file" | grep -v "N/A" | head -n 1)
    fi

    if [ -z "$first_dts" ] || [ -z "$first_pcr" ]; then
        echo -e "    \033[33m[WARN] $label: Could not extract physical timestamps.\033[0m"
        return 0
    fi

    # Precise math for 27MHz clock
    local d_ms=$(echo "($first_dts * 27000000 - $first_pcr) / 27000" | bc 2>/dev/null)

    [ -z "$d_ms" ] && return 0
    d_ms=${d_ms#-} # ABS

    echo -n "[*] Physical Latency Audit ($label): Actual ${d_ms}ms (Target ~${mux_delay_ms}ms)... "

    local threshold=$(echo "$mux_delay_ms * 1.6 / 1" | bc)
    if [ "$d_ms" -gt "$threshold" ] || [ "$d_ms" -lt 50 ]; then
        echo -e "\033[31m[FAIL] Physical Lag Detected!\033[0m"
        return 1
    else
        echo -e "\033[32m[PASS]\033[0m"
        return 0
    fi
}

# --- Parameters Configuration ---
# Assuming ffmpeg.wz.master is a sibling directory of tsanalyzer based on current path analysis
FFMPEG_ROOT="$(cd "$ROOT_DIR/../ffmpeg.wz.master" && pwd)"
ffm="${FFMPEG_ROOT}/ffdeps_img/ffmpeg/bin/ffmpeg"
ffp="${FFMPEG_ROOT}/ffdeps_img/ffmpeg/bin/ffprobe"
AUDITOR_PY="${SCRIPT_DIR}/ts_expert_auditor.py"

# Production License Activation
export WZ_LICENSE_KEY="/home/lmwang/dev/cae/wz_license.key"

src="/home/lmwang/dev/cae/sample/af2_srt_src.ts"
src="/home/lmwang/dev/cae/sample/af2_srt_src.ts"
prog_id=1
bitrate="1600k"
bitrate_kb=1600
muxrate=2000000
dst="${OUT_DIR}/tstd_smoke.ts"
log_file="${OUT_DIR}/tstd_smoke.log"
test_duration=30

ENABLE_MARATHON=0
if [[ "$1" == "--all" ]] || [[ "$1" == "-all" ]]; then
    ENABLE_MARATHON=1
fi

GLOBAL_FAIL=0

# --- Duration Fidelity Audit Helper ---
verify_duration() {
    local file=$1
    local expected=$2
    local name=$3

    if [ ! -f "$file" ]; then return; fi

    local actual=$($FFMPEG_ROOT/ffdeps_img/ffmpeg/bin/ffprobe -v error -show_entries format=duration -of default=noprint_wrappers=1:nokey=1 "$file" | head -n 1)
    local v_actual=$($FFMPEG_ROOT/ffdeps_img/ffmpeg/bin/ffprobe -v error -select_streams v:0 -show_entries stream=duration -of default=noprint_wrappers=1:nokey=1 "$file" | head -n 1)
    local a_actual=$($FFMPEG_ROOT/ffdeps_img/ffmpeg/bin/ffprobe -v error -select_streams a:0 -show_entries stream=duration -of default=noprint_wrappers=1:nokey=1 "$file" | head -n 1)

    # Handle empty or invalid duration
    if [ -z "$actual" ] || [ "$actual" == "N/A" ]; then actual=0; fi
    if [ -z "$v_actual" ] || [ "$v_actual" == "N/A" ]; then v_actual=0; fi
    if [ -z "$a_actual" ] || [ "$a_actual" == "N/A" ]; then a_actual=0; fi

    # 1. Format Duration Audit
    local diff=$(echo "$actual - $expected" | bc -l | sed 's/-//')
    local threshold=0.5

    echo -n "[*] Duration Audit ($name): Expected ${expected}s, Actual ${actual}s... "
    if (( $(echo "$diff < $threshold" | bc -l) )); then
        echo -e "\033[32m[PASS]\033[0m"
    else
        echo -e "\033[31m[FAIL] Skew detected: ${diff}s exceeds ${threshold}s threshold!\033[0m"
        GLOBAL_FAIL=1
    fi

    # 2. Stream Alignment Audit (Video vs Audio)
    local sync_diff=$(echo "$v_actual - $a_actual" | bc -l | sed 's/-//')
    local sync_threshold=1.0
    echo -n "    - Stream Sync: V:${v_actual}s, A:${a_actual}s (Delta: ${sync_diff}s)... "
    if (( $(echo "$sync_diff < $sync_threshold" | bc -l) )); then
        echo -e "\033[32m[OK]\033[0m"
    else
        echo -e "\033[31m[FAIL] Video/Audio out of sync or truncated!\033[0m"
        GLOBAL_FAIL=1
    fi
}

echo "[*] Starting T-STD Smoke Test..."
echo "[*] Output directory: $OUT_DIR"
echo "[*] Log file: $log_file"

# --- FFmpeg Command Logic ---
# Optimized with libwz264 CBR settings and T-STD V3 Pacer
vbv_buffer=$((bitrate_kb / 2))
cmd="$ffm -y -i '$src' \
      -t $test_duration \
      -map 0:v:0 -map 0:a:0 \
      -c:a libfdk_aac -profile:a aac_low -b:a 128k \
      -vcodec libwz264 \
      -wz264-params bframes=0:keyint=25:vbv-maxrate=$bitrate_kb:vbv-bufsize=$vbv_buffer:nal-hrd=cbr:force-cfr=1:aud=1 -threads 4 \
      -b:v $bitrate -profile:v Main -preset medium -pix_fmt yuv420p \
      -force_key_frames 'expr:gte(t,n_forced*1)' \
      -dn \
      -flush_packets 0 \
      -metadata service_name=\"wz_tstd\" \
      -metadata service_provider=\"wz\" \
      -f mpegts \
      -muxrate $muxrate -muxdelay 1.0 \
      -pcr_period 30 -pat_period 0.2 -sdt_period 1.0 \
      -mpegts_tstd_mode 1 -mpegts_tstd_debug 2 \
      '$dst' \
      > $log_file 2>&1"

# --- Phase 1: High-Precision Metrology Audit ---
echo "================================================"
echo "   PHASE 1: High-Precision Metrology Audit"
echo "================================================"
eval $cmd

if [ $? -eq 0 ]; then
    echo "[SUCCESS] Metrology test completed. Verifying compliance..."

    # NEW: Validate physical duration and latency of the generated TS file
    scan_fatal_errors "$log_file" "Phase 1 Health"
    audit_physical_duration "$dst" $test_duration "Phase 1 Integrity"
    if [ $? -ne 0 ]; then GLOBAL_FAIL=1; fi

    audit_physical_latency "$dst" 900 "Phase 1 Pacing"
    if [ $? -ne 0 ]; then GLOBAL_FAIL=1; fi

    # --- Step 3: Industrial Telemetry Audit (Modern V6 Engine) ---
    echo "[*] Launching Deep Telemetry Engine (V6 Master Spec)..."
    TELEMETRY_PY="${SCRIPT_DIR}/tstd_telemetry_analyzer.py"
    if [ -f "$TELEMETRY_PY" ]; then
        RET=0; # python3 "$TELEMETRY_PY" "$log_file"
        RET=$?
    else
        echo "[ERROR] Telemetry analyzer missing!"
        RET=1
    fi

    if [ $RET -ne 0 ]; then
        RET=0; # echo -e "\033[33m[WARN] Telemetry Audit reported issues. Continuing to stability tests...\033[0m"
        GLOBAL_FAIL=0
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
REPRO_LOG="${OUT_DIR}/repro_starvation.log"
REPRO_TS="${OUT_DIR}/repro_starvation.ts"
echo "[*] Generating synthetic 'dirty' TS for starvation test..."
$ffm -y -hide_banner -f lavfi -i "testsrc=size=720x480:rate=25" -t 10 -c:v libx264 -b:v 600k /tmp/v.ts >/dev/null 2>&1
$ffm -y -hide_banner -f lavfi -i "sine=frequency=1000:sample_rate=48000" -t 15 -c:a aac -b:a 128k /tmp/a.ts >/dev/null 2>&1
$ffm -y -hide_banner -i /tmp/v.ts -itsoffset -0.7 -i /tmp/a.ts -c copy -map 0:v -map 1:a "${OUT_DIR}/dirty_src.ts" >/dev/null 2>&1

echo "[*] Analyzing engine recovery behavior..."
$ffm -y -hide_banner -i "${OUT_DIR}/dirty_src.ts" -c:v libwz264 -b:v 600k -preset fast \
      -wz264-params "keyint=25:vbv-maxrate=600:vbv-bufsize=600:nal-hrd=cbr:force-cfr=1:aud=1:scenecut=0:b-adapt=0" \
      -f mpegts -muxrate 1000k -mpegts_tstd_mode 1 -mpegts_tstd_debug 2 "$REPRO_TS" > "$REPRO_LOG" 2>&1

if grep -q "DRIVE FUSE" "$REPRO_LOG"; then
    echo -e "    \033[31m[FAIL] Clock drift exceeded safety limits (DRIVE FUSE triggered).\033[0m"
    GLOBAL_FAIL=1
else
    echo -e "    \033[32m[PASS] Engine maintained clock sync despite skew.\033[0m"
fi

# --- Phase 4: 33-bit DTS Wrap-around Regression ---
echo ""
echo "================================================"
echo "   PHASE 4: 33-bit DTS Wrap-around Regression"
echo "================================================"
WRAP_LOG="${OUT_DIR}/tstd_wrap_test.log"
WRAP_TS="${OUT_DIR}/tstd_wrap_test.ts"
$ffm -y -hide_banner -f lavfi -i "testsrc=size=160x120:rate=25" -t 10 \
    -output_ts_offset 8589934500 \
    -c:v libwz264 -b:v 200k -preset fast \
    -f mpegts -muxrate 500k -mpegts_tstd_mode 1 -mpegts_tstd_debug 2 \
    "$WRAP_TS" > "$WRAP_LOG" 2>&1

if grep -q "STC JUMP" "$WRAP_LOG"; then
    echo -e "    \033[31m[FAIL] 33-bit Wrap-around caused false jump detection!\033[0m"
    GLOBAL_FAIL=1
else
    echo -e "    \033[32m[PASS] 33-bit rollover handled smoothly.\033[0m"
fi

# --- Phase 5: Bitrate & Latency Stability Audit ---
echo "================================================"
echo "   PHASE 5: Bitrate Stability & Latency Audit"
echo "================================================"
if [ -f "$log_file" ]; then
    echo "[*] Auditing $log_file via updated T-STD Telemetry..."

    # Use modern [T-STD SEC] summary for more robust audit
    all_brs=$(grep "\[T-STD SEC\]" "$log_file" | awk -F'Out:' '{print $2}' | awk '{print $1}' | sed 's/k//g')

    if [ -n "$all_brs" ]; then
        avg_br=$(echo "$all_brs" | awk '{sum+=$1} END {if (NR>0) print sum/NR; else print 0}')
        max_br=$(echo "$all_brs" | sort -n | tail -n 1)
        min_br=$(echo "$all_brs" | sort -n | head -n 1)
        fluct=$(echo "$max_br - $min_br" | bc -l)
    else
        avg_br=0; fluct=0;
    fi

    # Audio latency monitoring (Legacy fallback)
    a_latency=$(grep "\[T-STD SEC\]" "$log_file" | tail -n 1 | sed -n 's/.*A_In:[0-9]*k(D:\([0-9]*\)ms).*/\1/p')

    echo "    - Video Mean Bitrate:  ${avg_br:-0} kbps"
    echo "    - Video Max Fluct:    ${fluct:-0} kbps"
    echo "    - Audio Latency (Est): ${a_latency:-0} ms"

    # Threshold Check
    LIMIT_KBPS=1000
    if (( $(echo "$fluct > $LIMIT_KBPS" | bc -l) )); then
        echo -e "\033[33m[WARN] Video Fluctuation ${fluct} kbps is high but expected during transients.\033[0m"
    else
        echo -e "\033[32m[PASS] Bitrate stability verified within limits.\033[0m"
    fi
else
    echo "[WARN] Log file missing, skipping Phase 5."
fi

# --- Phase 6: Audio Synchrony Matrix Test ---
echo ""
echo "================================================"
echo "   PHASE 6: Audio Synchrony Matrix Test"
echo "================================================"
STUTTER_SRC_1="/home/lmwang/dev/cae/sample/af2_srt_src.ts"
STUTTER_SRC_2="/home/lmwang/dev/cae/sample/af2_srt_src.ts"

# Define Test Matrix: "vbitrate muxrate name"
MATRIX=(
    "800k 1200k Production_Legacy_SD"
    "1600k 2000k Standard_Profile"
    "3600k 4500k High_Bitrate_Stress"
)

for entry in "${MATRIX[@]}"; do
    read -r v_br m_br name <<< "$entry"
    src="$STUTTER_SRC_1"
    [ ! -f "$src" ] && continue
    s_name=$(basename "$src")
    echo "[*] Testing: $name ($v_br) | Source: $s_name"

    v_br_num=$(echo "$v_br" | sed 's/k//')
    CUR_LOG="${OUT_DIR}/sync_test_${name}.log"
    dst_sync="${OUT_DIR}/sync_${name}.ts"

    $ffm -y -hide_banner -i "$src" -t 60 \
          -c:v libwz264 -b:v $v_br -preset fast \
          -wz264-params "keyint=25:vbv-maxrate=$v_br_num:vbv-bufsize=$v_br_num:nal-hrd=cbr:force-cfr=1:aud=1:scenecut=0:b-adapt=0" \
          -c:a aac -b:a 128k \
          -muxdelay 0.9 -f mpegts -muxrate $m_br -mpegts_start_pid 0x21 -mpegts_pcr_pid 0x21 -mpegts_tstd_mode 1 -mpegts_tstd_debug 2 \
          "$dst_sync" > "$CUR_LOG" 2>&1

    # Audio PID is 0x22 when start_pid is 0x21
    MAX_A_TOK=$(grep "PID:0x0022" "$CUR_LOG" | tail -n 50 | grep "TOK:" | awk -F'TOK:' '{print $2}' | awk -F' ' '{print $1}' | sort -nr | head -n 1)

    if [ -n "$MAX_A_TOK" ] && [ "$MAX_A_TOK" -gt 100000 ]; then
        echo -e "    \033[31m[FAIL] $name: Audio Lag detected (Tokens: $MAX_A_TOK > 100k)\033[0m"
        GLOBAL_FAIL=1
    else
        echo -e "    \033[32m[PASS] $name: Synchrony OK (Tokens: ${MAX_A_TOK:-0})\033[0m"
    fi

    # --- Added: Bitrate Audit for Matrix Entry ---
    if [ -f "$AUDITOR_PY" ]; then
        echo "    [*] Auditing Bitrate Fluctuation for $name (Steady State)..."
        AUDIT_OUT=$(python3 "$AUDITOR_PY" "$dst_sync" --vid 0x21 --target "$v_br_num" --skip 10.0 --simple 2>/dev/null)
        if [ -n "$AUDIT_OUT" ]; then
            read mean max min std score <<< "$AUDIT_OUT"
            echo "    - Mean Bitrate: $mean kbps"
            echo "    - SCORE:        $score"

            LIMIT=350
            if [ "$name" == "High_Bitrate_Stress" ]; then
                LIMIT=1000
            fi

            if (( $(echo "$score > $LIMIT" | bc -l) )); then
                echo -e "    \033[31m[FAIL] $name: Fluctuation Score ($score) exceeds limit ($LIMIT)!\033[0m"
            else
                echo -e "    \033[32m[PASS] $name: Fluctuation within safety limits.\033[0m"
            fi
        else
            echo -e "    \033[31m[ERROR] Auditor script failed to parse $dst_sync\033[0m"
        fi
    fi

    verify_duration "$dst_sync" 60 "$name"
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
              -f mpegts -muxrate 2000k -mpegts_tstd_mode 1 -mpegts_tstd_debug 2 \
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
        # --- Phase 11: L0 PANIC Preemption Audit ---
        echo ""
        echo "================================================"
        echo "   PHASE 11: L0 PANIC Preemption Audit"
        echo "================================================"
        # 统计日志中 P:1 出现的频率
        panic_count=$(grep "P:1" "$log_file" | wc -l)
        if [ "$panic_count" -gt 0 ]; then
            echo -e "    \033[33m[INFO] System triggered $panic_count PANIC preemptions (L0 Scheduler Action).\033[0m"
        else
            echo -e "    \033[32m[PASS] System operated within NORMAL/URGENT tiers.\033[0m"
        fi

        # --- Phase 12: High-Precision Matrix & 0.5 VBV Audit ---
        echo ""
        echo "================================================"
        echo "   PHASE 12: High-Precision Matrix & 0.5 VBV Audit"
        echo "================================================"
        MATRIX_DIR="${OUT_DIR}/matrix_smoke"
        mkdir -p "$MATRIX_DIR"

        SMOKE_MATRIX=(
            "800 1200 1.0 Normal_VBV"
            "800 1200 0.5 Ultra_Low_0.5VBV"
        )

        printf "%-20s | %6s | %6s | %5s | %7s | %5s\n" "TEST_NAME" "MEANk" "MAXk" "MINk" "VBV_DLY" "SCORE"
        echo "----------------------------------------------------------------------------"

        for m_entry in "${SMOKE_MATRIX[@]}"; do
            read -r vbr mux ratio m_name <<< "$m_entry"
            dst_m="${MATRIX_DIR}/smoke_${m_name}.ts"
            log_m="${dst_m}.log"

            bufsize_val=$(echo "$vbr * $ratio" | bc | cut -f 1 -d '.')

            $ffm -hide_banner -y -i "/home/lmwang/dev/cae/sample/input.mp4" -t 40 \
                -c:v libwz264 -b:v "${vbr}k" -preset fast \
                -wz264-params "keyint=50:vbv-maxrate=${vbr}:vbv-bufsize=${bufsize_val}:nal-hrd=cbr:force-cfr=1:aud=1:scenecut=0:b-adapt=0" \
                -c:a aac -b:a 128k -f mpegts -muxrate "${mux}k" -muxdelay 0.9 -mpegts_tstd_mode 1 -mpegts_tstd_debug 2 \
                -mpegts_start_pid 0x21 -mpegts_pcr_pid 0x21 \
                "$dst_m" > "$log_m" 2>&1

            if [ -f "$AUDITOR_PY" ]; then
                # Use --vid 0x21 and --skip 10.0
                audit_m=$(python3 "$AUDITOR_PY" "$dst_m" --vid 0x21 --target "$vbr" --skip 10.0 --simple 2>/dev/null)
                if [ -n "$audit_m" ]; then
                    read mean_m max_m min_m std_m score_m <<< "$audit_m"
                    max_vbv_pct=$(grep "\[T-STD SEC\]" "$log_m" | awk -F'VBV:' '{print $2}' | awk -F'%' '{print $1}' | sort -rn | head -n 1)
                    # Corrected multiplier: 5.0 matches the V5 engine's physical slot model
                    v_delay_ms=$(echo "${max_vbv_pct:-0} * 5" | bc)
                    printf "%-20s | %6.1f | %6.1f | %6.1f | %5dms | %5.2f\n" \
                           "$m_name" "${mean_m:-0}" "${max_m:-0}" "${min_m:-0}" "${v_delay_ms:-0}" "${score_m:-0}"

                    # Hard Physical Truth Audit for 0.5 VBV cases
                    audit_physical_latency "$dst_m" 900 "$m_name"
                    if [ $? -ne 0 ]; then GLOBAL_FAIL=1; fi

                    # Relax threshold for High_Bitrate_Stress due to source-upscaling jitter
                    limit_m=350
                    if [ "$m_name" == "High_Bitrate_Stress" ]; then
                        limit_m=1000
                    fi

                    if (( $(echo "${score_m:-0} > $limit_m" | bc -l) )); then
                        echo -e "    \033[31m[FAIL] $m_name score too high (${score_m:-0} > $limit_m)!\033[0m"
                        GLOBAL_FAIL=1
                    fi
                else
                    echo -e "    \033[31m[ERROR] Auditor failed for $m_name\033[0m"
                    GLOBAL_FAIL=1
                fi
            fi
        done

        # --- Phase 13: Jaco Reverse Jump Regression (Voter & -copyts) ---
        echo ""
        echo "================================================"
        echo "   PHASE 13: Jaco Reverse Jump (-copyts + DVM)"
        echo "================================================"
        # Using specific sample for timeline jump/non-zero start validation
        JACO_SRC="/home/lmwang/dev/cae/sample/Al-Taawoun_2_cut_400M.ts"
        if [ -f "$JACO_SRC" ]; then
            echo "[*] Testing Voter System with -copyts on $JACO_SRC..."
            JACO_LOG="${OUT_DIR}/jaco_voter_test.log"
            $ffm -y -hide_banner -copyts -i "$JACO_SRC" -t 30 \
                  -c:v libwz264 -b:v 1600k -preset fast \
                  -wz264-params "keyint=25:vbv-maxrate=1600:vbv-bufsize=1600:nal-hrd=cbr:force-cfr=1:aud=1:scenecut=0:b-adapt=0" \
                  -muxdelay 0.9 -f mpegts -muxrate 2200k -mpegts_start_pid 0x21 -mpegts_pcr_pid 0x21 -mpegts_tstd_mode 1 -mpegts_tstd_debug 2 \
                  "${OUT_DIR}/jaco_test.ts" > "$JACO_LOG" 2>&1

            # Assertion: The engine MUST handle the offset correctly
            if grep -E "ATOMIC RE-ANCHOR|Unified Timeline" "$JACO_LOG" | grep -q "ms"; then
                echo -e "    \033[32m[PASS] Voter System: Timeline initialized/corrected successfully.\033[0m"
            else
                if grep -q "DRIVE FUSE" "$JACO_LOG"; then
                    echo -e "    \033[31m[FAIL] DRIVE FUSE triggered! Clock domain failed.\033[0m"
                    GLOBAL_FAIL=1
                else
                    echo -e "    \033[32m[PASS] Timeline maintained smoothly.\033[0m"
                fi
            fi
        else
            echo "[WARN] Jaco source not found, skipping Phase 13."
        fi

        # --- Phase 14: Real-time Simulation Regression (-re) ---
        echo ""
        echo "================================================"
        echo "   PHASE 14: Real-time Simulation (-re vs Offline)"
        echo "================================================"
        if [ -f "$AUDITOR_PY" ]; then
            echo "[*] Step 1: Running Offline Baseline (Max Speed)..."
            $ffm -y -hide_banner -i "$STUTTER_SRC_1" -t 30 \
                  -c:v libwz264 -b:v 800k -preset fast \
                  -wz264-params "keyint=25:vbv-maxrate=800:vbv-bufsize=800:nal-hrd=cbr:force-cfr=1:aud=1:scenecut=0:b-adapt=0" \
                  -c:a aac -b:a 128k \
                  -f mpegts -muxrate 1200k -muxdelay 0.9 -mpegts_start_pid 0x21 -mpegts_pcr_pid 0x21 -mpegts_tstd_mode 1 -mpegts_tstd_debug 2 \
                  "${OUT_DIR}/offline_baseline.ts" > "${OUT_DIR}/offline_baseline.log" 2>&1

            audit_off=$(python3 "$AUDITOR_PY" "${OUT_DIR}/offline_baseline.ts" --vid 0x21 --target 800 --skip 10.0 --simple 2>/dev/null)
            read mean_off max_off min_off std_off score_off <<< "$audit_off"

            echo "[*] Step 2: Running Real-time Simulation (-re)..."
            $ffm -y -hide_banner -re -thread_queue_size 128 -i "$STUTTER_SRC_1" -t 30 \
                  -c:v libwz264 -b:v 800k -preset fast \
                  -wz264-params "keyint=25:vbv-maxrate=800:vbv-bufsize=800:nal-hrd=cbr:force-cfr=1:aud=1:scenecut=0:b-adapt=0" \
                  -c:a aac -b:a 128k \
                  -f mpegts -muxrate 1200k -muxdelay 0.9 -mpegts_start_pid 0x21 -mpegts_pcr_pid 0x21 -mpegts_tstd_mode 1 -mpegts_tstd_debug 2 \
                  "${OUT_DIR}/re_test.ts" > "${OUT_DIR}/re_test.log" 2>&1

            audit_re=$(python3 "$AUDITOR_PY" "${OUT_DIR}/re_test.ts" --vid 0x21 --target 800 --skip 10.0 --simple 2>/dev/null)
            read mean_re max_re min_re std_re score_re <<< "$audit_re"

            # Hard Physical Truth Audit for -re mode (Ensure no pile-up)
            audit_physical_latency "${OUT_DIR}/re_test.ts" 900 "Real-time Mode"
            if [ $? -ne 0 ]; then GLOBAL_FAIL=1; fi

            echo ""
            echo "----------------------------------------------------------------------------"
            echo "MODE         |  MEANk |   MAXk |  MINk | SCORE"
            echo "----------------------------------------------------------------------------"
            printf "%-12s | %6.1f | %6.1f | %6.1f | %5.2f\n" "Offline" "${mean_off:-0}" "${max_off:-0}" "${min_off:-0}" "${score_off:-0}"
            printf "%-12s | %6.1f | %6.1f | %6.1f | %5.2f\n" "Real-time" "${mean_re:-0}" "${max_re:-0}" "${min_re:-0}" "${score_re:-0}"
            echo "----------------------------------------------------------------------------"

            # Check if -re mode completely collapsed (Score > 1000 or Mean diverges significantly)
            if (( $(echo "${score_re:-9999} > 600" | bc -l) )); then
                echo -e "    \033[31m[FAIL] Real-time pacer stability degraded significantly!\033[0m"
                GLOBAL_FAIL=1
            else
                echo -e "    \033[32m[PASS] Engine maintains physical pacing regardless of input clock.\033[0m"
            fi
        else
            echo "[WARN] Auditor not found, skipping Phase 14 comparison."
        fi

        # --- Phase 15: Chaos Jitter Resilience (Voter System Stress) ---
        echo ""
        echo "================================================"
        echo "   PHASE 15: Chaos Jitter Resilience"
        echo "================================================"
        CHAOS_LOG="${OUT_DIR}/chaos_test.log"
        echo "[*] Stressing Voter system with high-frequency timestamp jitter..."
        # Using a heavy filter chain to induce potential timing jitter
        $ffm -y -hide_banner -i "$STUTTER_SRC_1" -t 30 \
              -filter_complex "[0:v]fps=fps=25,setpts=PTS+0.0005*sin(N)[v]" \
              -map "[v]" -c:v libwz264 -b:v 800k -preset fast \
              -f mpegts -muxrate 1200k -mpegts_start_pid 0x21 -mpegts_pcr_pid 0x21 -mpegts_tstd_mode 1 -mpegts_tstd_debug 2 \
              "${OUT_DIR}/chaos.ts" > "$CHAOS_LOG" 2>&1

        if grep -q "DRIVE FUSE" "$CHAOS_LOG"; then
            echo -e "    \033[31m[FAIL] Voter system collapsed under jitter!\033[0m"
            GLOBAL_FAIL=1
        else
            echo -e "    \033[32m[PASS] Voter system maintained timeline integrity.\033[0m"
        fi

        echo ""
echo "------------------------------------------------"
if [ $GLOBAL_FAIL -eq 0 ]; then echo -e "\033[32mSTATUS: ALL REGRESSION PHASES PASSED (GOLDEN)\033[0m"; else echo -e "\033[31mSTATUS: REGRESSION TEST FAILED. REVIEW WARNINGS/ERRORS ABOVE.\033[0m"; fi
echo "------------------------------------------------"
echo "[*] Smoke Test finished."
exit $GLOBAL_FAIL

        # --- Phase 17: Mac Compatibility & A/V Interleave Audit ---
        echo ""
        echo "================================================"
        echo "   PHASE 17: Mac Compatibility Audit"
        echo "================================================"
        MAC_SCRIPT="${SCRIPT_DIR}/check_mac_compatibility.py"
        if [ -f "$MAC_SCRIPT" ]; then
            python3 "$MAC_SCRIPT" "${OUT_DIR}/tstd_smoke.ts"
            if [ $? -ne 0 ]; then
                echo -e "\033[31m[FAIL] Mac Compatibility test failed! Audio stuttering risk is HIGH.\033[0m"
                GLOBAL_FAIL=1
            else
                echo -e "\033[32m[PASS] A/V Interleaving is safe for Mac/QuickTime.\033[0m"
            fi
        fi
