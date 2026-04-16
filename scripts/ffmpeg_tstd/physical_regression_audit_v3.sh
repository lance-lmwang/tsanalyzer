#!/bin/bash
FFMPEG_ROOT="/home/lmwang/dev/cae/ffmpeg.wz.master"
ffm="${FFMPEG_ROOT}/ffdeps_img/ffmpeg/bin/ffmpeg"
ffp="${FFMPEG_ROOT}/ffdeps_img/ffmpeg/bin/ffprobe"
SRC="/home/lmwang/dev/cae/sample/af2_srt_src.ts"
OUT_DIR="/home/lmwang/dev/cae/tsanalyzer/output/physical_regress"
mkdir -p "$OUT_DIR"

echo "MODE | GOP | VBV | MEAN_BR | MAX_BR | MIN_BR | FLUCT"
echo "----------------------------------------------------"

run_benchmark() {
    local mode=$1
    local name=$2
    local dst="${OUT_DIR}/m${mode}.ts"

    # 统一使用 GOP 1s, VBV 1s (600)
    $ffm -hide_banner -y -i "$SRC" -t 30 \
        -c:v libwz264 -b:v 600k -preset fast \
        -force_key_frames "expr:eq(mod(n,25),0)" \
        -wz264-params "keyint=25:min-keyint=25:vbv-maxrate=600:vbv-bufsize=600:nal-hrd=cbr:force-cfr=1:aud=1" \
        -f mpegts -muxrate 1100k -mpegts_tstd_mode $mode -mpegts_start_pid 0x21 \
        "$dst" > /dev/null 2>&1

    # 获取每个视频包的 DTS 时间戳和大小
    # 然后按照 1秒 窗口累加包的大小 (188 bytes/packet)
    # 单位转换为 kbps (bytes * 8 / 1000)
    local results=$($ffp -v error -select_streams v:0 -show_packets -show_entries packet=dts_time,size "$dst" | \
        grep -E "dts_time|size" | paste - - | sed 's/dts_time=//;s/size=//' | \
        awk '{t=int($1); sum[t]+=$2} END {for(i in sum) if(i>0 && i<29) print sum[i]*8/1000}')

    local mean=$(echo "$results" | awk '{sum+=$1} END {print sum/NR}')
    local max=$(echo "$results" | sort -nr | head -n 1)
    local min=$(echo "$results" | sort -n | head -n 1)
    local fluct=$(echo "$max - $min" | bc -l)

    printf "%4s | %3s | %3s | %7.2f | %6.2f | %6.2f | %5.2f\n" "$mode" "25" "600" "$mean" "$max" "$min" "$fluct"
}

run_benchmark 0 "Native"
run_benchmark 1 "Strict"
run_benchmark 2 "Elastic"
