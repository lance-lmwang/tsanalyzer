#!/bin/bash
# T-STD Bitrate Stability: Flush Impact Contrast Matrix
# Usage: ./ffmpeg_tstd_bitrate_stability.sh [src] [v_bitrate] [muxrate] [tstd_mode]

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
OUT_DIR="${ROOT_DIR}/output/flush_contrast"
mkdir -p "$OUT_DIR"

# 1. Parameter Resolution
SRC=$1
if [ -z "$SRC" ]; then
    SRC="/home/lmwang/dev/cae/sample/af2_srt_src.ts"
    [ ! -f "$SRC" ] && SRC="/home/lmwang/dev/cae/sample/knet_sd_03.ts"
fi

V_BITRATE=${2:-"1600k"}
MUXRATE=${3:-"2000000"}
MODE=${4:-"1"}
DURATION=15

# Path Resolution
FFMPEG_ROOT="$(cd "$ROOT_DIR/../ffmpeg.wz.master" && pwd)"
ffm="${FFMPEG_ROOT}/ffmpeg"
if [ ! -f "$ffm" ]; then ffm="${FFMPEG_ROOT}/ffdeps_img/ffmpeg/bin/ffmpeg"; fi
analyzer="${SCRIPT_DIR}/ts_pid_bitrate_pcr_analyzer.py"

echo "================================================================"
echo "   T-STD REGRESSION: IO FLUSH CONTRAST MATRIX"
echo "================================================================"
echo "[*] Input Source: $SRC"
echo "[*] Target: $V_BITRATE @ $MUXRATE (Mode $MODE)"
echo "----------------------------------------------------------------"

run_test() {
    local flush=$1
    local name="Flush_$flush"
    local log="${OUT_DIR}/${name}.log"
    local ts="${OUT_DIR}/${name}.ts"

    echo -n "[*] Running with -flush_packets $flush... "

    $ffm -y -hide_banner -v trace -i "$SRC" -t $DURATION \
          -c:v libwz264 -b:v $V_BITRATE -preset medium \
          -wz264-params bframes=0:keyint=25:vbv-maxrate=${V_BITRATE%k}:vbv-bufsize=${V_BITRATE%k}:nal-hrd=cbr \
          -c:a aac -b:a 128k \
          -flush_packets $flush \
          -f mpegts -muxrate $MUXRATE -muxdelay 0.9 -pcr_period 30 -mpegts_tstd_mode $MODE \
          "$ts" > "$log" 2>&1

    if [ $? -eq 0 ]; then echo "Done."; else echo "FAILED."; fi

    # Analyze results
    echo "    - [Physical Audit] "
    python3 "$analyzer" "$ts" --pid 0x0100 --pcr 0x0100 --skip 5.0 | grep "Fluctuation" | sed 's/^/      /'
}

# 运行对比矩阵
run_test 1 # 默认刷新
run_test 0 # 禁止刷新 (生产高压模拟)

echo "----------------------------------------------------------------"
echo "分析提示：如果 Flush 0 波动远大于 Flush 1，证明波动源自 IO 积压而非引擎算法。"
