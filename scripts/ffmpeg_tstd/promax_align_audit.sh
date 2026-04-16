#!/bin/bash
# T-STD V3 对齐 PROMAX 工业标准审计脚本
# 对齐参数: 滑动窗口 1504ms, 基于包序号滑动

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
OUT_DIR="${ROOT_DIR}/output/promax_alignment"
mkdir -p "$OUT_DIR"

FFMPEG_ROOT="$(cd "$ROOT_DIR/../ffmpeg.wz.master" && pwd)"
ffm="${FFMPEG_ROOT}/ffdeps_img/ffmpeg/bin/ffmpeg"
ffp="${FFMPEG_ROOT}/ffdeps_img/ffmpeg/bin/ffprobe"
SRC="/home/lmwang/dev/cae/sample/af2_srt_src.ts"

echo "MODE | WINDOW | MEAN_BR | MAX_BR | MIN_BR | FLUCTUATION"
echo "----------------------------------------------------------"

run_benchmark() {
    local mode=$1
    local dst="${OUT_DIR}/m${mode}.ts"

    # 保持实验条件一致: GOP 1s (25), VBV 1s (600)
    $ffm -hide_banner -y -i "$SRC" -t 30 \
        -c:v libwz264 -b:v 600k -preset fast \
        -force_key_frames "expr:eq(mod(n,25),0)" \
        -wz264-params "keyint=25:min-keyint=25:vbv-maxrate=600:vbv-bufsize=600:nal-hrd=cbr:force-cfr=1:aud=1" \
        -f mpegts -muxrate 1100k -mpegts_tstd_mode $mode -mpegts_start_pid 0x21 \
        "$dst" > /dev/null 2>&1

    # 核心对齐：提取包的 DTS 和 Size，按 1504ms 滑动窗口统计
    # 模拟 PROMAX 的 1504ms 滑动窗口行为
    $ffp -v error -select_streams v:0 -show_packets -show_entries packet=dts_time,size "$dst" | \
        grep -E "dts_time|size" | paste - - | sed 's/dts_time=//;s/size=//' | \
        awk '
        BEGIN {window=1.504; step=0.05}
        { ts[NR]=$1; sz[NR]=$2; }
        END {
            # 以 50ms 为步长滑动窗口
            for (w_start=5.0; w_start<25.0; w_start+=step) {
                w_end = w_start + window;
                bits = 0;
                for (i=1; i<=NR; i++) {
                    if (ts[i] >= w_start && ts[i] < w_end) bits += sz[i] * 8;
                }
                kbps = bits / (window * 1.0);
                if (max_f == 0 || kbps > max_f) max_f = kbps;
                if (min_f == 0 || kbps < min_f) min_f = kbps;
                all_sum += kbps;
                w_count++;
            }
            printf "%7.2f | %6.2f | %6.2f | %11.2f\n", all_sum/w_count, max_f, min_f, max_f-min_f
        }' > result.tmp

    printf "%4s | 1.504s | %s\n" "$mode" "$(cat result.tmp)"
    echo "----------------------------------------------------------"
}

run_benchmark 0 "Native"
run_benchmark 1 "Strict"
run_benchmark 2 "Elastic"
EOF
bash /home/lmwang/dev/cae/tsanalyzer/scripts/ffmpeg_tstd/tstd_v3_validation.sh
