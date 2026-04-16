#!/bin/bash
FFMPEG_ROOT="/home/lmwang/dev/cae/ffmpeg.wz.master"
ffm="${FFMPEG_ROOT}/ffdeps_img/ffmpeg/bin/ffmpeg"
ffp="${FFMPEG_ROOT}/ffdeps_img/ffmpeg/bin/ffprobe"
SRC="/home/lmwang/dev/cae/sample/af2_srt_src.ts"
OUT_DIR="/home/lmwang/dev/cae/tsanalyzer/output/multi_window_audit"
mkdir -p "$OUT_DIR"

echo "MODE | WINDOW | MEAN_BR | MAX_BR | MIN_BR | FLUCTUATION"
echo "--------------------------------------------------------"

run_benchmark() {
    local mode=$1
    local name=$2
    local dst="${OUT_DIR}/m${mode}.ts"

    # 执行转码 (GOP 1s, VBV 1s)
    $ffm -hide_banner -y -i "$SRC" -t 30 \
        -c:v libwz264 -b:v 600k -preset fast \
        -force_key_frames "expr:eq(mod(n,25),0)" \
        -wz264-params "keyint=25:min-keyint=25:vbv-maxrate=600:vbv-bufsize=600:nal-hrd=cbr:force-cfr=1:aud=1" \
        -f mpegts -muxrate 1100k -mpegts_tstd_mode $mode -mpegts_start_pid 0x21 \
        "$dst" > /dev/null 2>&1

    # 提取所有视频包的数据 (DTS, Size)
    local raw_data=$($ffp -v error -select_streams v:0 -show_packets -show_entries packet=dts_time,size "$dst" | \
        grep -E "dts_time|size" | paste - - | sed 's/dts_time=//;s/size=//')

    # 计算 1.0s 窗口统计
    echo "$raw_data" | awk '{t=int($1); sum[t]+=$2} END {for(i in sum) if(i>0 && i<29) print sum[i]*8/1000}' > stats_1s.tmp
    local mean_1s=$(awk '{s+=$1} END {print s/NR}' stats_1s.tmp)
    local max_1s=$(sort -nr stats_1s.tmp | head -n 1)
    local min_1s=$(sort -n stats_1s.tmp | head -n 1)
    local fluct_1s=$(echo "$max_1s - $min_1s" | bc -l)
    printf "%4s |  1.0s  | %7.2f | %6.2f | %6.2f | %11.2f\n" "$mode" "$mean_1s" "$max_1s" "$min_1s" "$fluct_1s"

    # 计算 0.5s 窗口统计
    echo "$raw_data" | awk '{t=int($1 * 2) / 2; sum[t]+=$2} END {for(i in sum) if(i>0 && i<28.5) print sum[i]*2*8/1000}' > stats_05s.tmp
    local mean_05s=$(awk '{s+=$1} END {print s/NR}' stats_05s.tmp)
    local max_05s=$(sort -nr stats_05s.tmp | head -n 1)
    local min_05s=$(sort -n stats_05s.tmp | head -n 1)
    local fluct_05s=$(echo "$max_05s - $min_05s" | bc -l)
    printf "%4s |  0.5s  | %7.2f | %6.2f | %6.2f | %11.2f\n" "$mode" "$mean_05s" "$max_05s" "$min_05s" "$fluct_05s"

    echo "--------------------------------------------------------"
    rm -f *.tmp
}

run_benchmark 0 "Native"
run_benchmark 1 "Strict"
run_benchmark 2 "Elastic"
