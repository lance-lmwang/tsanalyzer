#!/bin/bash
FFMPEG_ROOT="/home/lmwang/dev/cae/ffmpeg.wz.master"
ffm="${FFMPEG_ROOT}/ffdeps_img/ffmpeg/bin/ffmpeg"
ffp="${FFMPEG_ROOT}/ffdeps_img/ffmpeg/bin/ffprobe"
SRC="/home/lmwang/dev/cae/sample/af2_srt_src.ts"
DST="/home/lmwang/dev/cae/tsanalyzer/output/promax_align.ts"

# 1. 以 Strict 模式，VBV 150ms 转码 (对齐 64k 波动的最佳候选)
$ffm -hide_banner -y -i "$SRC" -t 30 \
    -c:v libwz264 -b:v 600k -preset fast \
    -force_key_frames "expr:eq(mod(n,25),0)" \
    -wz264-params "keyint=25:min-keyint=25:vbv-maxrate=600:vbv-bufsize=150:nal-hrd=cbr:force-cfr=1:aud=1" \
    -f mpegts -muxrate 1100k -mpegts_tstd_mode 1 -mpegts_start_pid 0x21 \
    "$DST" > /dev/null 2>&1

echo "TIME(s) | BITRATE(kbps)"
echo "-----------------------"
# 2. 输出 1.5s 窗口下的每 50ms 滑动序列，模拟 PROMAX 屏幕采样
$ffp -v error -select_streams v:0 -show_packets -show_entries packet=dts_time,size "$DST" | \
    grep -E "dts_time|size" | paste - - | sed 's/dts_time=//;s/size=//' | \
    awk '
    BEGIN {window=1.5; step=0.05}
    { ts[NR]=$1; sz[NR]=$2; }
    END {
        for (w_start=5.0; w_start<15.0; w_start+=step) {
            w_end = w_start + window;
            bits = 0;
            for (i=1; i<=NR; i++) {
                if (ts[i] >= w_start && ts[i] < w_end) bits += sz[i] * 8;
            }
            printf "%7.2f | %8.2f\n", w_start, bits/window/1000.0;
        }
    }'
