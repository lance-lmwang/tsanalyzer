#!/bin/bash
# T-STD V3 PCR-Based PROMAX Alignment Matrix (GOP 1s, VBV 1s)

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
OUT_DIR="${ROOT_DIR}/output/promax_matrix"
mkdir -p "$OUT_DIR"

FFMPEG_ROOT="$(cd "$ROOT_DIR/../ffmpeg.wz.master" && pwd)"
ffm="${FFMPEG_ROOT}/ffdeps_img/ffmpeg/bin/ffmpeg"
AUDITOR="${SCRIPT_DIR}/ffmpeg_tstd_pcr_sliding_window.py"
SRC="/home/work/jinzicai/FFmpeg/SRT_PUSH_KNET_SD-s2ejvr9_20260311-13.04.03.ts"

if [ ! -f "$SRC" ]; then
    echo "[!] Warning: User specified SRC not found, falling back to af2_srt_src.ts"
    SRC="/home/lmwang/dev/cae/sample/af2_srt_src.ts"
fi

echo "=============================================================================="
echo "  T-STD PRODUCTION PARAMETERS AUDIT (PCR Time, 1504ms Window)"
echo "=============================================================================="
echo "MODE | GOP | VBV | MEAN_BR(bps) | MAX_BR(bps) | MIN_BR(bps) | FLUCTUATION(bps)"
echo "------------------------------------------------------------------------------"

run_mode() {
    local mode=$1
    local name=$2
    local dst="${OUT_DIR}/m${mode}_prod.ts"

    # 使用用户提供的完整生产参数 (去除 -re 提高转码速度，限定 -t 60 秒保证足够审计数据)
    $ffm -hide_banner -y -thread_queue_size 128 -rw_timeout 30000000 -fflags +discardcorrupt \
        -i "$SRC" -t 60 \
        -metadata comment=wzcaetrans \
        -filter_complex '[0:v]fps=fps=25[fg_0_fps];[fg_0_fps]wzaipreopt=enhtype=WZ_FaceMask_MNN:expandRatio=0.1:speedLvlFace=1,wzoptimize=autoenh=0:ynslvl=0:uvnslvl=0:uvenh=0:sharptype=3:yenh=1.6:thrnum=2[fg_0_custom]' \
        -map '[fg_0_custom]' -c:v:0 libwz264 -g:v:0 25 -force_key_frames:v:0 'expr:if(mod(n,25),0,1)' \
        -preset:v:0 fast -wz264-params:v:0 'keyint=25:min-keyint=25:aq-mode=2:aq-weight=0.4:aq-strength=1.0:aq-smooth=1.0:psy-rd=0.3:psy-rd-roi=0.4:qcomp=0.65:rc-lookahead=10:pbratio=1.1:vbv-maxrate=600:vbv-bufsize=600:nal-hrd=cbr:force-cfr=1:aud=1:scenecut=0:b-adapt=0' \
        -map 0:a -c:a:0 copy -map '0:d?' -c:d copy -pes_payload_size 0 -threads 2 -pix_fmt yuv420p -color_range tv \
        -b:v 600k -flush_packets 0 -muxrate 1100k -inputbw 0 -oheadbw 25 \
        -maxbw 0 -latency 1000000 -muxdelay 0.9 -pcr_period 30 -pat_period 0.2 -sdt_period 0.25 \
        -mpegts_start_pid 0x21 -mpegts_tstd_mode $mode -mpegts_tstd_debug 2 -max_muxing_queue_size 4096 -max_interleave_delta 30000000 \
        -f mpegts "$dst" > /dev/null 2>&1

    # 专家级 PCR 审计 (1504ms 滑动窗口)
    local audit=$(python3 "$AUDITOR" "$dst" --vid_pid 0x21 --pcr_pid 0x21 --window_ms 1504.0 --skip_sec 3.0 --mode promax --muxrate 1100000)

    read mean max_b min_b fluct <<< $(echo "$audit" | awk -F':' '
    /Mean Bitrate/ {m=$2}
    /Max Bitrate/ {x=$2}
    /Min Bitrate/ {n=$2}
    /Fluctuation/ {f=$2}
    END {
       split(m,a," "); split(x,b," "); split(n,c," "); split(f,d," ");
       print a[1], b[1], c[1], d[1]
    }')

    printf "%4s |  1s |  1s | %12.2f | %11.2f | %11.2f | %16.2f\n" "$mode" "${mean:-0}" "${max_b:-0}" "${min_b:-0}" "${fluct:-0}"
}

#run_mode 0 "Native"
run_mode 1 "Strict"
#run_mode 2 "Elastic"
echo "------------------------------------------------------------------------------"
