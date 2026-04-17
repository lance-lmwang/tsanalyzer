#!/bin/bash
# T-STD High-Precision Audit Tool (Physical Slot Emulation)

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
OUT_DIR="${ROOT_DIR}/output/promax_matrix"
mkdir -p "$OUT_DIR"

FFMPEG_ROOT="$(cd "$ROOT_DIR/../ffmpeg.wz.master" && pwd)"
ffm="${FFMPEG_ROOT}/ffdeps_img/ffmpeg/bin/ffmpeg"
AUDITOR="${SCRIPT_DIR}/ts_expert_auditor.py"

SRC="${1:-/home/lmwang/dev/cae/sample/knet_sd_03.ts}"
VBR_TARGET="${2:-800}"
MUX_TARGET="${3:-1200}"
DUR="${4:-120}"
MODE="${5:-1}"

SRC_BASE=$(basename "$SRC" | cut -f 1 -d '.')

print_header() {
    echo "MODE | V_BIT | MUX |  MEANk |   MAXk |   MINk |  STDk | SCORE"
    echo "----------------------------------------------------------------------------"
}

run_mux() {
    local mode=$1 vbr=$2 mux=$3
    local vbr_val=$(echo "$vbr" | sed 's/[kKmM]//g')
    local mux_val=$(echo "$mux" | sed 's/[kKmM]//g')
    local dst="${OUT_DIR}/${SRC_BASE}_m${mode}_v${vbr_val}.ts"
    $ffm -hide_banner -stream_loop -1 -y -thread_queue_size 128 -rw_timeout 30000000 -fflags +discardcorrupt \
        -i "$SRC" -t "$DUR" \
        -preset fast -wz264-params "keyint=25:vbv-maxrate=${vbr_val}:vbv-bufsize=$((vbr_val/2)):nal-hrd=cbr" \
        -b:v "${vbr_val}k" -muxrate "${mux_val}k" -muxdelay 0.9 -pcr_period 30 \
        -mpegts_start_pid 0x21 -mpegts_tstd_mode "$mode" -mpegts_tstd_debug 2 \
        -f mpegts "$dst" > "${dst}.log" 2>&1
}

run_audit() {
    local mode=$1 vbr=$2 mux=$3
    local vbr_val=$(echo "$vbr" | sed 's/[kKmM]//g')
    local dst="${OUT_DIR}/${SRC_BASE}_m${mode}_v${vbr_val}.ts"

    if [ ! -f "$dst" ]; then return; fi

    # 1. 如果是单次执行，输出详细的按秒及异常报告
    if [ "$SINGLE_RUN" = "1" ]; then
        python3 "$AUDITOR" "$dst" --vid 0x21 --target "$vbr_val"
    fi

    # 2. 获取汇总数据用于表格展示
    local audit=$(python3 "$AUDITOR" "$dst" --vid 0x21 --target "$vbr_val" --simple)
    read mean max min std score <<< $audit

    printf "%4s | %5s | %3s | %6s | %6s | %6s | %5s | %5s\n" \
           "$mode" "$vbr_val" "$mux" "$mean" "$max" "$min" "$std" "$score"
}

if [ -n "$2" ] || [ -n "$3" ]; then
    SINGLE_RUN=1
    print_header
    run_mux "$MODE" "$VBR_TARGET" "$MUX_TARGET"
    run_audit "$MODE" "$VBR_TARGET" "$MUX_TARGET"
else
    echo "[*] Running High-Precision Matrix Audit (Serial Mode)..."
    SINGLE_RUN=0
    # 串行运行以保证物理层采样精度，不被 IO 竞争污染
    run_mux 1 800 1200
    run_mux 1 1000 1400
    run_mux 1 1300 1700

    print_header
    run_audit 1 800 1200
    run_audit 1 1000 1400
    run_audit 1 1300 1700
fi
