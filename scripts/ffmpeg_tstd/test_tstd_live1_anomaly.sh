#!/bin/bash
# ==============================================================================
# T-STD Regression Test: Real-World "Dirty Stream" Anomaly (live1)
# ==============================================================================
# Purpose:
#   Test the T-STD engine's resilience against severe input anomalies commonly
#   found in production environments, using the 'knet_sd_03.ts' sample.
#
# Anomalies in this stream:
#   1. Severe asymmetric A/V start skew (-700ms audio lead).
#   2. Corrupt video frames and non-existing PPS.
#   3. Sudden Video stream death near EOF while Audio continues.
#   4. Narrow pipe constraints (1200k Muxrate vs 1154k Peak).
#
# Expected Outcome:
#   - The Starvation Detection must catch the sudden death.
#   - The engine MUST NOT trigger the fatal 'DRIVE FUSE' (-4.8s skew) collapse.
#   - The final file size must be optimal (~17.9MB), not padded with 20s of NULLs.
# ==============================================================================

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
FFMPEG="${ROOT_DIR}/../ffmpeg.wz.master/ffdeps_img/ffmpeg/bin/ffmpeg"
OUT_DIR="${ROOT_DIR}/output"
OUT_TS="${OUT_DIR}/live1.ts"
OUT_LOG="${OUT_DIR}/live1.log"
SRC_FILE="${ROOT_DIR}/../sample/knet_sd_03.ts"

mkdir -p "$OUT_DIR"

echo "[*] ==========================================================="
echo "[*] T-STD Regression: Real-World Anomaly Test (live1 scenario)"
echo "[*] ==========================================================="

if [ ! -f "$SRC_FILE" ]; then
    echo -e "\033[33m[SKIP] Source file not found: $SRC_FILE. Skipping anomaly test.\033[0m"
    exit 0
fi

echo "[*] Processing severely degraded source: $SRC_FILE"
echo "    - Testing Mode 1 (Strict CBR) resilience..."

$FFMPEG -y -hide_banner \
    -thread_queue_size 128 -rw_timeout 30000000 -fflags +discardcorrupt \
    -i "$SRC_FILE" \
    -filter_complex '[0:v]fps=fps=25[fg_0_fps];[fg_0_fps]wzaipreopt=enhtype=WZ_FaceMask_MNN:expandRatio=0.1:speedLvlFace=1,wzoptimize=autoenh=0:ynslvl=0:uvnslvl=0:uvenh=0:sharptype=3:yenh=1.6:thrnum=2[fg_0_custom]' \
    -map [fg_0_custom] -c:v:0 libwz264 -force_key_frames:v:0 'expr:eq(mod(n,25),0)' \
    -preset:v:0 fast -wz264-params:v:0 'keyint=25:min-keyint=25:aq-mode=2:aq-weight=0.4:aq-strength=1.0:aq-smooth=1.0:psy-rd=0.3:psy-rd-roi=0.4:qcomp=0.65:rc-lookahead=10:pbratio=1.1:vbv-maxrate=600:vbv-bufsize=600:nal-hrd=cbr:force-cfr=1:aud=1:scenecut=0:b-adapt=0' \
    -map 0:a -c:a:0 copy -map 0:d? -c:d copy -pes_payload_size 0 \
    -threads 2 -pix_fmt yuv420p -color_range tv \
    -b:v 600k -flush_packets 0 -muxrate 1200k -inputbw 0 -oheadbw 25 \
    -maxbw 0 -latency 1000000 -muxdelay 0.9 -pcr_period 18 -pat_period 0.2 -sdt_period 0.25 \
    -mpegts_start_pid 0x21 -max_muxing_queue_size 4096 -max_interleave_delta 30000000 \
    -mpegts_tstd_mode 1 \
    -f mpegts "$OUT_TS" > "$OUT_LOG" 2>&1

EXIT_CODE=$?
echo ""

if [ $EXIT_CODE -ne 0 ]; then
    echo -e "\033[31m[CRITICAL] FFmpeg process crashed (Exit Code: $EXIT_CODE)!\033[0m"
    exit 1
fi

echo "[*] Analyzing engine telemetry for anomaly recovery..."

# 1. Fatal Clock Drift Check
if grep -q "DRIVE FUSE" "$OUT_LOG"; then
    echo -e "\033[31m[FAIL] Fatal clock desync (DRIVE FUSE) triggered! Engine failed to recover from dirty source.\033[0m"
    grep "DRIVE FUSE" "$OUT_LOG"
    exit 1
else
    echo -e "\033[32m[PASS] Engine maintained clock sync despite severe source anomalies.\033[0m"
fi

# 2. Starvation Detection Check (Must Trigger on this dirty file)
if grep -q "starvation detected" "$OUT_LOG"; then
    echo -e "\033[32m[PASS] Starvation Detection engaged successfully, preventing stall.\033[0m"
else
    echo -e "\033[31m[FAIL] Starvation Detection failed to trigger on a known broken stream!\033[0m"
    exit 1
fi

# 3. File Size Sanity Check (Ensure EOF Fast Drain worked)
# The output should be around 17.9MB if drained correctly, not 23.8MB.
FILE_SIZE=$(stat -c%s "$OUT_TS")
MAX_ALLOWED=19000000 # 19.0 MB limit

if [ $FILE_SIZE -gt $MAX_ALLOWED ]; then
    echo -e "\033[31m[FAIL] File size ($FILE_SIZE bytes) is bloated. EOF Fast Drain failed.\033[0m"
    exit 1
else
    echo -e "\033[32m[PASS] File size ($FILE_SIZE bytes) is optimal (<19MB). No NULL explosion.\033[0m"
fi

echo "-----------------------------------------------------------"
echo -e "\033[32mSTATUS: SUCCESS (Real-world dirty stream handled perfectly)\033[0m"
exit 0
