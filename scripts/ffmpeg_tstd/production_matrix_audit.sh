#!/bin/bash
# T-STD Production Matrix Audit (V2)
# Comparing GOP and VBV effects across T-STD modes.

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
OUT_DIR="${ROOT_DIR}/output/matrix_audit"
mkdir -p "$OUT_DIR"

FFMPEG_ROOT="$(cd "$ROOT_DIR/../ffmpeg.wz.master" && pwd)"
ffm="${FFMPEG_ROOT}/ffdeps_img/ffmpeg/bin/ffmpeg"
ffp="${FFMPEG_ROOT}/ffdeps_img/ffmpeg/bin/ffprobe"
# 修正审计脚本路径
analyzer="${SCRIPT_DIR}/tstd_bitrate_auditor.py"

SRC="/home/lmwang/dev/cae/sample/af2_srt_src.ts"
DURATION=20  # 缩短时长以加快反馈

echo "MODE | GOP | VBV | MEAN_BR | FLUCTUATION"
echo "---------------------------------------"

run_test() {
    local mode=$1
    local gop=$2
    local vbv=$3
    local name="m${mode}_g${gop}_v${vbv}"
    local dst="${OUT_DIR}/${name}.ts"
    local log="${OUT_DIR}/${name}.log"

    # Construct Params
    local cmd="$ffm -hide_banner -y -i '$SRC' -t $DURATION \
        -c:v libwz264 -b:v 600k -preset fast \
        -force_key_frames 'expr:eq(mod(n,$gop),0)' \
        -wz264-params 'keyint=$gop:min-keyint=$gop:vbv-maxrate=600:vbv-bufsize=$vbv:nal-hrd=cbr:force-cfr=1:aud=1:scenecut=0:b-adapt=0' \
        -c:a copy -muxrate 1100k -muxdelay 0.9 -mpegts_tstd_debug 2 "

    [ "$mode" -gt 0 ] && cmd="$cmd -mpegts_tstd_mode $mode"
    cmd="$cmd -f mpegts '$dst'"

    eval $cmd > "$log" 2>&1

    # Audit using the correct python script
    local audit=$(python3 "$analyzer" --log "$log" --pid 0x0100 --window 1.0 --skip 5.0)
    local mean=$(echo "$audit" | grep "Mean Bitrate" | awk '{print $3}')
    local fluct=$(echo "$audit" | grep "Fluctuation" | awk '{print $2}')

    printf "%4s | %3s | %3s | %7s | %10s\n" "$mode" "$gop" "$vbv" "$mean" "$fluct"
}

# Matrix Execution
for gop in 25 50; do
    for vbv in 600 300; do
        for mode in 0 1 2; do
            run_test $mode $gop $vbv
        done
        echo "---------------------------------------"
    done
done
