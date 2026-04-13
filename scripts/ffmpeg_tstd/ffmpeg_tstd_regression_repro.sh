#!/bin/bash
# T-STD Comparative Analysis (Aligned with live1.sh)
# Purpose: Compare Native vs T-STD Mode 1 & 2 using exact production params.

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
OUT_DIR="${ROOT_DIR}/output/bug_analysis"
mkdir -p "$OUT_DIR"

FFMPEG_ROOT="$(cd "$ROOT_DIR/../ffmpeg.wz.master" && pwd)"
ffm="${FFMPEG_ROOT}/ffdeps_img/ffmpeg/bin/ffmpeg"
if [ ! -f "$ffm" ]; then ffm="${FFMPEG_ROOT}/ffmpeg"; fi

SRC="/home/lmwang/dev/cae/sample/knet_sd_03.ts"
[ ! -f "$SRC" ] && SRC="/home/lmwang/dev/cae/sample/af2_srt_src.ts"

DURATION=60
echo "================================================================"
echo "   PRODUCTION ALIGNED COMPARATIVE TEST (0 vs 1 vs 2)"
echo "================================================================"
echo "[*] Source: $SRC"
echo "[*] Using ffmpeg: $ffm"

# --- Exact Copy of live1.sh Base Params (WITHOUT -re) ---
BASE_PARAMS="-hide_banner -y -thread_queue_size 128 -rw_timeout 30000000 -fflags +discardcorrupt -i '$SRC' \
      -metadata comment=wzcaetrans \
      -filter_complex '[0:v]fps=fps=25[fg_0_fps];[fg_0_fps]wzoptimize=autoenh=0:ynslvl=0:uvnslvl=0:uvenh=0:sharptype=3:yenh=1.6:thrnum=2[fg_0_custom]' \
      -map [fg_0_custom] -c:v:0 libwz264 -force_key_frames:v:0 'expr:eq(mod(n,25),0)' \
      -preset:v:0 fast -wz264-params:v:0 'keyint=25:min-keyint=25:aq-mode=2:aq-weight=0.4:aq-strength=1.0:aq-smooth=1.0:psy-rd=0.3:psy-rd-roi=0.4:qcomp=0.65:rc-lookahead=10:pbratio=1.1:vbv-maxrate=600:vbv-bufsize=600:nal-hrd=cbr:force-cfr=1:aud=1:scenecut=0:b-adapt=0' \
      -map 0:a -c:a:0 copy -map 0:d? -c:d copy -pes_payload_size 0 -threads 2 -pix_fmt yuv420p -color_range tv \
      -b:v 600k -flush_packets 0 -muxrate 1300k -inputbw 0 -oheadbw 25 \
      -maxbw 0 -latency 1000000 -muxdelay 0.9 -pcr_period 18 -pat_period 0.2 -sdt_period 0.25 \
      -mpegts_start_pid 0x21 -max_muxing_queue_size 4096 -max_interleave_delta 30000000 -t $DURATION"

run_mode() {
    local mode=$1
    local name=$2
    local dst="${OUT_DIR}/live_m${mode}.ts"
    local log="${OUT_DIR}/live_m${mode}.log"

    echo ""
    echo "[*] RUNNING MODE $mode ($name)..."
    local cmd="$ffm $BASE_PARAMS"
    if [ "$mode" -gt 0 ]; then
        cmd="$cmd -mpegts_tstd_mode $mode -f mpegts '$dst'"
    else
        cmd="$cmd -f mpegts '$dst'"
    fi

    eval $cmd > "$log" 2>&1

    # 1. Duration Check
    local actual=$($FFMPEG_ROOT/ffdeps_img/ffmpeg/bin/ffprobe -v error -show_entries format=duration -of default=noprint_wrappers=1:nokey=1 "$dst")
    echo "[RESULT] Actual Duration: ${actual}s (Target: ${DURATION}s)"

    # 2. Bitrate Fluctuation Check (Physical)
    echo "[RESULT] Physical Bitrate Audit (PID 0x21):"
    python3 scripts/ffmpeg_tstd/ts_pid_bitrate_pcr_analyzer.py "$dst" --pid 0x0021 --pcr 0x0021 --skip 5.0 | grep -E "Mean Bitrate|Fluctuation"
}

# Run the matrix
run_mode 0 "Native"
run_mode 1 "Strict"
run_mode 2 "Elastic"

echo ""
echo "================================================================"
echo "分析提示：对比 Mode 0 和 1/2 的 Duration。如果 1/2 明显偏短，说明存在 Skew 丢包。"
echo "对比 Fluctuation。如果 Mode 1 比 Mode 0 波动大，说明 T-STD 调度算法有抖动。"
