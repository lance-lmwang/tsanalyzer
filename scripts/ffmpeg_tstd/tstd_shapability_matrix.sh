#!/bin/bash

# 配置路径
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
FFMPEG_ROOT="$(cd "$ROOT_DIR/../ffmpeg.wz.master" && pwd)"

FFMPEG="${FFMPEG_ROOT}/ffdeps_img/ffmpeg/bin/ffmpeg"
FFPROBE="${FFMPEG_ROOT}/ffdeps_img/ffmpeg/bin/ffprobe"
SAMPLE_DIR="${ROOT_DIR}/../sample"
OUTPUT_DIR="${ROOT_DIR}/output"
mkdir -p "$OUTPUT_DIR"

# 测试矩阵执行函数
run_test() {
    NAME=$1
    FILE=$2
    BV=$3
    MUX=$4
    PCR=$5
    VBV_OVERRIDE=$6

    # 智能参数解析：检查第6个参数是否为数字覆盖
    if [[ "$VBV_OVERRIDE" =~ ^[0-9]+$ ]]; then
        VBV_BUF=$VBV_OVERRIDE
        # 物理对齐：MUXDELAY = VBV / Bitrate
        MUXDELAY=$(awk "BEGIN {printf \"%.2f\", $VBV_BUF / ${BV%k}}")
        shift 6
    else
        MUXDELAY=0.9
        VBV_BUF=$(awk "BEGIN {printf \"%d\", ${BV%k} * $MUXDELAY}")
        shift 5
    fi
    EXTRA_ARGS="$@"


    echo "========================================================="
    echo " RUNNING TEST CASE: $NAME"
    echo " Input: $FILE, Video: $BV, Mux: $MUX, Muxdelay: ${MUXDELAY}s, VBV Buffer: ${VBV_BUF}k, PCR: ${PCR}ms"
    echo "========================================================="

    INPUT_PATH="$SAMPLE_DIR/$FILE"
    if [ ! -f "$INPUT_PATH" ]; then
        echo "Error: Input file $INPUT_PATH not found, skipping."
        return
    fi

    LOG_FILE="$OUTPUT_DIR/tstd_${NAME}_md${MUXDELAY}.v2.log"
    OUT_TS="$OUTPUT_DIR/tstd_${NAME}_md${MUXDELAY}.v2.ts"

    # 执行转码 (Industrial V2 settings)
    $FFMPEG -y -hide_banner -thread_queue_size 128 -rw_timeout 30000000 -fflags +discardcorrupt \
        -i "$INPUT_PATH" \
        -metadata comment=wzcaetrans \
        -filter_complex "[0:v]fps=fps=25 [fg_0_fps];[fg_0_fps]wzaipreopt=enhtype=WZ_FaceMask_MNN:expandRatio=0.1:speedLvlFace=1,wzoptimize=autoenh=0:ynslvl=0:uvnslvl=0:uvenh=0:sharptype=3:yenh=1.6:thrnum=4[fg_0_custom]" \
        -map [fg_0_custom] -c:v:0 libwz264 -g:v:0 25 -force_key_frames:v:0 'expr:if(mod(n,25),0,1)' -preset:v:0 fast \
        -wz264-params:v:0 "keyint=25:min-keyint=25:aq-mode=2:aq-weight=0.4:aq-strength=1.0:aq-smooth=1.0:psy-rd=0.3:psy-rd-roi=0.4:qcomp=0.65:rc-lookahead=10:pbratio=1.1:vbv-maxrate=${BV%k}:vbv-bufsize=${VBV_BUF}:nal-hrd=cbr:force-cfr=1:aud=1:scenecut=0:b-adapt=0" \
        -map 0:a -c:a:0 copy $EXTRA_ARGS \
        -threads 4 -pix_fmt yuv420p -color_range tv -b:v $BV \
        -flush_packets 0 -muxrate $MUX -muxdelay $MUXDELAY -pcr_period $PCR \
        -pat_period 0.2 -sdt_period 0.25 -mpegts_start_pid 0x21 -mpegts_tstd_mode 1 -tstd_params "debug=1:window=30" \
        -max_muxing_queue_size 4096 -max_interleave_delta 30000000 \
        -f mpegts "$OUT_TS" > "$LOG_FILE" 2>&1

    sync
    echo "Test finished. Analyzing Result..."

    OUT_IMG="${OUT_TS%.ts}.jpg"
    DATA_FILE="${OUT_TS%.ts}.tmp"

    echo "--- 30-PCR Window Bitrate Stats ---"
    # 提取 In 码率, Out 码率, Pkt 和 V-Dly 进行统计和绘图
    grep "\[T-STD SEC\]" "$LOG_FILE" | sed -n 's/.*In:[[:space:]]*\([0-9]*\)k.*Out:[[:space:]]*\([0-9]*\)k.*Pkt:[[:space:]]*\([0-9]*\).*V-Dly:[[:space:]]*\([0-9]*\)ms.*/\1 \2 \3 \4/p' > "$DATA_FILE.raw"

    if [ -s "$DATA_FILE.raw" ]; then
        # 生成绘图数据 (X: Sample Index, Y1: Out Bitrate, Y2: In Bitrate)
        awk '{print NR-1, $2, $1}' "$DATA_FILE.raw" > "$DATA_FILE"

        # 调用 gnuplot 生成 JPG 图表
        gnuplot << EOF
set terminal jpeg size 1400,600
set output "$OUT_IMG"
set title "T-STD Bitrate (30-PCR Windowed: In vs Out) | Muxdelay=${MUXDELAY}s"
set xlabel "Sample Index (30-PCR Windows)"
set ylabel "Bitrate (kbps)"
set grid xtics ytics mytics
set key outside
# 每 100k 显示一个带文字的刻度
set ytics 100
# 将 100k 分为 5 份，每份 20k（产生 20k 间隔的辅助网格线）
set mytics 5
set xrange [0:*]
plot "$DATA_FILE" using 1:2 with lines title "Output (Paced)" lw 2 lt 1 lc rgb "green", \
     "$DATA_FILE" using 1:3 with lines title "Input (Raw)" lw 1 lt 2 lc rgb "red"
EOF
        echo "Bitrate JPG saved to $OUT_IMG"
        rm "$DATA_FILE"

        # 计算极值和平均值 (Skip first 5 samples/seconds to ignore startup artifacts)
        tail -n +6 "$DATA_FILE.raw" | \
        awk '{
            in_bps=$1; out=$2; pkt=$3; vdly=$4;
            if(min_p==""){min_p=max_p=pkt; min_o=max_o=out; max_v=vdly};
            if(pkt<min_p)min_p=pkt; if(pkt>max_p)max_p=pkt;
            if(out<min_o)min_o=out; if(out>max_o)max_o=out;
            if(vdly>max_v)max_v=vdly;
            sum_p+=pkt; sum_o+=out; count++
        } END {
            if(count>0) {
                printf "Packet Stats  : Min: %d, Max: %d, Delta: %d, Avg: %.1f\n", min_p, max_p, max_p-min_p, sum_p/count;
                printf "Bitrate Stats : Min: %dk, Max: %dk, Delta: %dk, Avg: %.1f k (Target Delta < 88k)\n", min_o, max_o, max_o-min_o, sum_o/count;
                printf "Delay Stats   : Max Video Delay: %dms (Target < 2000ms)\n", max_v;
            } else {
                print "Result: No valid data for stats."
            }
        }'

        # 高阶输入突发特征分析 (Advanced Burst Analysis)
        python3 "$SCRIPT_DIR/tsa_shapability_analyzer.py" "$LOG_FILE" "$MUX" "$MUXDELAY"

        # 执行物理层时钟合规性审计 (PCR vs DTS Violation Detection)
        "$SCRIPT_DIR/offline_clock_audit.sh" "$OUT_TS"

        rm "$DATA_FILE.raw"
    else
        echo "Result: No window data found in log."
    fi
}

# 命令行逻辑 (KNET-Aligned Targets)
case "$1" in
    sd)    run_test "sd" "SRT_PUSH_AURORA-ZBX_KNET_SD-s6rmgxr_20260312-16.18.04.ts" "600k" "1100k" 35 -flags +ilme+ildct ;;
    720p)  run_test "720p" "HD720p_4Mbps.ts" "1300k" "1700k" 35 ;;
    1080i) run_test "1080i" "hd-2026.3.13-10.20~10.25.ts" "1500k" "2300k" 35 -flags +ilme+ildct ;;
    1080p_high) run_test "1080p_high" "hd-2026.3.13-10.20~10.25.ts" "4000k" "4800k" 35 2400 ;;
    all)
        $0 sd
        $0 720p
        $0 1080i
        $0 1080p_high
        ;;
    *)
        echo "Usage: $0 {sd|720p|1080i|all}"
        exit 1
        ;;
esac
