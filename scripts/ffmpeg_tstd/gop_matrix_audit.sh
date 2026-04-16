#!/bin/bash
FFMPEG_ROOT="/home/lmwang/dev/cae/ffmpeg.wz.master"
ffm="${FFMPEG_ROOT}/ffdeps_img/ffmpeg/bin/ffmpeg"
ffp="${FFMPEG_ROOT}/ffdeps_img/ffmpeg/bin/ffprobe"
SRC="/home/lmwang/dev/cae/sample/af2_srt_src.ts"
OUT_DIR="/home/lmwang/dev/cae/tsanalyzer/output/gop_matrix"
mkdir -p "$OUT_DIR"

echo "GOP | MODE | WINDOW | MEAN_BR | MAX_BR | MIN_BR | FLUCTUATION"
echo "------------------------------------------------------------"

run_benchmark() {
    local gop=$1
    local mode=$2
    local name="g${gop}_m${mode}"
    local dst="${OUT_DIR}/${name}.ts"

    # 执行转码 (VBV 1s 固定)
    $ffm -hide_banner -y -i "$SRC" -t 30 \
        -c:v libwz264 -b:v 600k -preset fast \
        -force_key_frames "expr:eq(mod(n,$gop),0)" \
        -wz264-params "keyint=$gop:min-keyint=$gop:vbv-maxrate=600:vbv-bufsize=600:nal-hrd=cbr:force-cfr=1:aud=1" \
        -f mpegts -muxrate 1100k -mpegts_tstd_mode $mode -mpegts_start_pid 0x21 \
        "$dst" > /dev/null 2>&1

    # 物理层滑动窗口审计 (1.0s Window, 0.1s Step)
    $ffp -v error -select_streams v:0 -show_packets -show_entries packet=dts_time,size "$dst" | \
        grep -E "dts_time|size" | paste - - | sed 's/dts_time=//;s/size=//' | \
        awk '
        BEGIN {window=1.0; step=0.1; max_fluct=0; min_fluct=1e10; sum_total=0; w_count=0}
        { ts[NR]=$1; sz[NR]=$2; }
        END {
            for (w_start=5.0; w_start<25.0; w_start+=step) {
                w_end = w_start + window;
                current_bits = 0;
                for (i=1; i<=NR; i++) {
                    if (ts[i] >= w_start && ts[i] < w_end) current_bits += sz[i] * 8;
                }
                current_kbps = current_bits / 1000.0;
                if (current_kbps > max_fluct) max_fluct = current_kbps;
                if (current_kbps < min_fluct) min_fluct = current_kbps;
                all_sum += current_kbps;
                w_count++;
            }
            mean = all_sum / w_count;
            fluct = max_fluct - min_fluct;
            printf "%7.2f | %6.2f | %6.2f | %11.2f\n", mean, max_fluct, min_fluct, fluct
        }' > result.tmp

    local stats=$(cat result.tmp)
    printf "%3s | %4s |  1.0s  | %s\n" "$gop" "$mode" "$stats"
}

# 运行 GOP 1s 和 2s 的全模式对比
for g in 25 50; do
    for m in 0 1 2; do
        run_benchmark $g $m
    done
    echo "------------------------------------------------------------"
done
