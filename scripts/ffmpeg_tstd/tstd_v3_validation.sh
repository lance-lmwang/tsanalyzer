#!/bin/bash
# T-STD V3 控制逻辑重构验证脚本
# 配置: GOP 1s, VBV 1s, 视频 600k, Muxrate 1100k
# 目标: 对比 Mode 0, 1, 2 在物理层滑动窗口下的码率波动

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
OUT_DIR="${ROOT_DIR}/output/v3_validation"
mkdir -p "$OUT_DIR"

FFMPEG_ROOT="$(cd "$ROOT_DIR/../ffmpeg.wz.master" && pwd)"
ffm="${FFMPEG_ROOT}/ffdeps_img/ffmpeg/bin/ffmpeg"
ffp="${FFMPEG_ROOT}/ffdeps_img/ffmpeg/bin/ffprobe"
SRC="/home/lmwang/dev/cae/sample/af2_srt_src.ts"

# 如果 Docker 编译后的路径不对，尝试备用路径
[ ! -f "$ffm" ] && ffm="${FFMPEG_ROOT}/ffmpeg"

echo "=========================================================="
echo "   T-STD V3 CONTROL LOGIC VALIDATION (GOP 1s, VBV 1s)"
echo "=========================================================="
echo "MODE | WINDOW | MEAN_BR | MAX_BR | MIN_BR | FLUCTUATION"
echo "----------------------------------------------------------"

run_benchmark() {
    local mode=$1
    local name=$2
    local dst="${OUT_DIR}/m${mode}.ts"

    # 1. 转码 (严格执行 GOP 1s, VBV 1s)
    $ffm -hide_banner -y -i "$SRC" -t 30 \
        -c:v libwz264 -b:v 600k -preset fast \
        -force_key_frames "expr:eq(mod(n,25),0)" \
        -wz264-params "keyint=25:min-keyint=25:vbv-maxrate=600:vbv-bufsize=600:nal-hrd=cbr:force-cfr=1:aud=1" \
        -f mpegts -muxrate 1100k -mpegts_tstd_mode $mode -mpegts_start_pid 0x21 \
        "$dst" > /dev/null 2>&1

    # 2. 提取物理层数据 (DTS, Size)
    local raw_data=$($ffp -v error -select_streams v:0 -show_packets -show_entries packet=dts_time,size "$dst" | \
        grep -E "dts_time|size" | paste - - | sed 's/dts_time=//;s/size=//')

    # 3. 滑动窗口审计函数 (Awk 实现)
    audit_window() {
        local win=$1
        echo "$raw_data" | awk -v window="$win" '
        BEGIN {step=0.1; max_f=0; min_f=1e10; sum_total=0; w_count=0}
        { ts[NR]=$1; sz[NR]=$2; }
        END {
            for (w_start=5.0; w_start<25.0; w_start+=step) {
                w_end = w_start + window;
                bits = 0;
                for (i=1; i<=NR; i++) {
                    if (ts[i] >= w_start && ts[i] < w_end) bits += sz[i] * 8;
                }
                kbps = bits / (window * 1000.0);
                if (kbps > max_f) max_f = kbps;
                if (kbps < min_f) min_f = kbps;
                all_sum += kbps;
                w_count++;
            }
            printf "%7.2f | %6.2f | %6.2f | %11.2f\n", all_sum/w_count, max_f, min_f, max_f-min_f
        }'
    }

    # 输出 1.0s 窗口结果
    local res_1s=$(audit_window 1.0)
    printf "%4s |  1.0s  | %s\n" "$mode" "$res_1s"

    # 输出 0.5s 窗口结果
    local res_05s=$(audit_window 0.5)
    printf "%4s |  0.5s  | %s\n" "$mode" "$res_05s"

    echo "----------------------------------------------------------"
}

# 依次执行三种模式
run_benchmark 0 "Native"
run_benchmark 1 "Strict"
run_benchmark 2 "Elastic"

echo "[*] Validation complete. Analysis files in $OUT_DIR"
