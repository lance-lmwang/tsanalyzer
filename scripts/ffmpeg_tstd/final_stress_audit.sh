#!/bin/bash
FFMPEG_ROOT="/home/lmwang/dev/cae/ffmpeg.wz.master"
ffm="${FFMPEG_ROOT}/ffdeps_img/ffmpeg/bin/ffmpeg"
ffp="${FFMPEG_ROOT}/ffdeps_img/ffmpeg/bin/ffprobe"
SRC="/home/lmwang/dev/cae/sample/af2_srt_src.ts"
OUT_DIR="/home/lmwang/dev/cae/tsanalyzer/output/final_stress"
mkdir -p "$OUT_DIR"

echo "VBV | MODE | WINDOW | MEAN_BR | FLUCTUATION"
echo "--------------------------------------------"

# 使用 150ms (VBV=150) 和 1.0s (600) 进行对比
for vbv in 150 600; do
    for mode in 1; do # 仅测试 Strict 模式
        dst="${OUT_DIR}/v${vbv}_m${mode}.ts"
        $ffm -hide_banner -y -i "$SRC" -t 30 \
            -c:v libwz264 -b:v 600k -preset fast \
            -wz264-params "keyint=25:vbv-maxrate=600:vbv-bufsize=$vbv:nal-hrd=cbr" \
            -f mpegts -muxrate 1100k -mpegts_tstd_mode $mode -mpegts_start_pid 0x21 \
            "$dst" > /dev/null 2>&1

        # 计算 1.5s 物理窗口波动
        fluct=$($ffp -v error -select_streams v:0 -show_packets -show_entries packet=dts_time,size "$dst" | \
            grep -E "dts_time|size" | paste - - | sed 's/dts_time=//;s/size=//' | \
            awk 'BEGIN {w=1.5} {ts[NR]=$1; sz[NR]=$2} END {
                max_f=0; min_f=1e10;
                for(s=5; s<25; s+=0.1) {
                    bits=0; for(i=1; i<=NR; i++) if(ts[i]>=s && ts[i]<s+w) bits+=sz[i]*8;
                    kbps=bits/w/1000;
                    if(kbps>max_f) max_f=kbps; if(kbps<min_f) min_f=kbps;
                }
                print max_f-min_f
            }')
        printf "%3s | %4s | 1.5s | %10.2f\n" "$vbv" "$mode" "$fluct"
    done
done
