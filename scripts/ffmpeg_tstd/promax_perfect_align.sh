#!/bin/bash
# 最终对齐：包含总封装码率，1504ms 滑动窗口，10ms 步长
FFMPEG_ROOT="/home/lmwang/dev/cae/ffmpeg.wz.master"
ffm="${FFMPEG_ROOT}/ffdeps_img/ffmpeg/bin/ffmpeg"
ffp="${FFMPEG_ROOT}/ffdeps_img/ffmpeg/bin/ffprobe"
SRC="/home/lmwang/dev/cae/sample/af2_srt_src.ts"
DST="/home/lmwang/dev/cae/tsanalyzer/output/perfect_align.ts"

# 1. 转码 (Mode 1 Strict, 严格 CBR 填充)
$ffm -hide_banner -y -i "$SRC" -t 30 \
    -c:v libwz264 -b:v 600k -preset fast \
    -force_key_frames "expr:eq(mod(n,25),0)" \
    -wz264-params "keyint=25:min-keyint=25:vbv-maxrate=600:vbv-bufsize=150:nal-hrd=cbr:force-cfr=1:aud=1" \
    -f mpegts -muxrate 1100k -mpegts_tstd_mode 1 -mpegts_start_pid 0x21 \
    "$DST" > /dev/null 2>&1

# 2. 物理层审计：提取所有包 (包含 NULL/SI)，按 1504ms 窗口统计总带宽 (bps)
echo "INDEX | PROMAX_ALIGNED_BPS"
$ffp -v error -show_packets -show_entries packet=pts_time,size "$DST" | \
    grep -E "pts_time|size" | paste - - | sed 's/pts_time=//;s/size=//' | \
    awk '
    BEGIN {window=1.504; step=0.01}
    { ts[NR]=$1; sz[NR]=$2; }
    END {
        for (w_start=5.0; w_start<8.0; w_start+=step) {
            w_end = w_start + window;
            bytes = 0;
            for (i=1; i<=NR; i++) {
                if (ts[i] >= w_start && ts[i] < w_end) bytes += sz[i];
            }
            # 转换为 bps (bytes * 8 / 1.504s)
            printf "%5d | %12.0f\n", w_start*100, (bytes*8)/1.504;
        }
    }'
