#!/bin/bash
# T-STD Production Regression Suite (V3)
# Purpose: Compare Native(0), Strict(1), and Elastic(2) with production params.
# Metrics: Duration fidelity and Physical Bitrate Smoothness.

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
OUT_DIR="${ROOT_DIR}/output/regression_prod"
mkdir -p "$OUT_DIR"

# Path Resolution
FFMPEG_ROOT="$(cd "$ROOT_DIR/../ffmpeg.wz.master" && pwd)"
ffm="${FFMPEG_ROOT}/ffdeps_img/ffmpeg/bin/ffmpeg"
if [ ! -f "$ffm" ]; then ffm="${FFMPEG_ROOT}/ffmpeg"; fi
ffp="${FFMPEG_ROOT}/ffdeps_img/ffmpeg/bin/ffprobe"
analyzer="${SCRIPT_DIR}/ts_pid_bitrate_pcr_analyzer.py"

# Test Settings
SRC="/home/lmwang/dev/cae/sample/daxiang.mp4"
SRC="/home/lmwang/dev/cae/sample/knet_sd_03.ts"
[ ! -f "$SRC" ] && SRC="/home/lmwang/dev/cae/sample/af2_srt_src.ts"
DURATION=540

echo "================================================================"
echo "   T-STD PRODUCTION REGRESSION TEST"
echo "================================================================"
echo "[*] Source: $SRC"
echo "[*] Target Duration: ${DURATION}s"
echo "[*] Using ffmpeg: $ffm"
echo "----------------------------------------------------------------"

# --- Production Base Params (NO -re for faster/cleaner logic test) ---
BASE_PARAMS="-hide_banner -stream_loop -1 -y -thread_queue_size 128 -rw_timeout 30000000 -fflags +discardcorrupt -i '$SRC' \
      -filter_complex '[0:v]fps=fps=25[fg_0_fps];[fg_0_fps]wzoptimize=autoenh=0:ynslvl=0:uvnslvl=0:uvenh=0:sharptype=3:yenh=1.6:thrnum=2[fg_0_custom]' \
      -map [fg_0_custom] -c:v:0 libwz264 -force_key_frames:v:0 'expr:eq(mod(n,25),0)' \
      -preset:v:0 fast -wz264-params:v:0 'keyint=25:min-keyint=25:aq-mode=2:aq-weight=0.4:aq-strength=1.0:aq-smooth=1.0:psy-rd=0.3:psy-rd-roi=0.4:qcomp=0.65:rc-lookahead=10:pbratio=1.1:vbv-maxrate=600:vbv-bufsize=600:nal-hrd=cbr:force-cfr=1:aud=1:scenecut=0:b-adapt=0' \
      -map 0:a -c:a:0 copy -map 0:d? -c:d copy -pes_payload_size 0 -threads 2 -pix_fmt yuv420p -color_range tv \
      -a53cc 0 -b:v 600k -flush_packets 0 -muxrate 1100k -inputbw 0 -oheadbw 25 \
      -maxbw 0 -latency 1000000 -muxdelay 0.9 -pcr_period 30 -pat_period 0.2 -sdt_period 0.25 \
      -mpegts_start_pid 0x21 -max_muxing_queue_size 4096 -max_interleave_delta 30000000 -t $DURATION"

run_benchmark() {
    local mode=$1
    local name=$2
    local dst="${OUT_DIR}/regress_m${mode}.ts"
    local log="${OUT_DIR}/regress_m${mode}.log"

    echo -n "[*] Testing Mode $mode ($name)... "
    local cmd="$ffm $BASE_PARAMS"
    [ "$mode" -gt 0 ] && cmd="$cmd -mpegts_tstd_mode $mode"
    [ "$mode" -eq 1 ] && cmd="$cmd -mpegts_tstd_debug 2"
    cmd="$cmd -f mpegts '$dst'"

    eval $cmd > "$log" 2>&1
    if [ $? -eq 0 ]; then echo "Done."; else echo "FAILED."; return; fi
}

# Execute Test Matrix
#run_benchmark 0 "Native"
run_benchmark 1 "Strict"

echo "----------------------------------------------------------------"
echo "[*] All regression tests finished. Logs in $OUT_DIR"
