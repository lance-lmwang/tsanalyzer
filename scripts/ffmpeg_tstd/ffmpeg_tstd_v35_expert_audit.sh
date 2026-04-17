#!/bin/bash
# T-STD v3.5 Deterministic Shaper Expert Audit Tool
# 验证核心: 分数阶时钟, FLL 频率锁, 二阶 ΣΔ, 全局耦合约束

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
OUT_DIR="${ROOT_DIR}/output/expert_audit_v35"
mkdir -p "$OUT_DIR"

FFMPEG_ROOT="$(cd "$ROOT_DIR/../ffmpeg.wz.master" && pwd)"
ffm="${FFMPEG_ROOT}/ffdeps_img/ffmpeg/bin/ffmpeg"
AUDITOR="${SCRIPT_DIR}/ffmpeg_tstd_pcr_sliding_window.py"

# --- 配置区 ---
SRC="${1:-/home/lmwang/dev/cae/sample/knet_sd_03.ts}"
VBR_TARGET="800k"
MUX_TARGET="1200k"
DUR="120"  # 120s 长时验证

# 颜色定义
RED='\033[0;31m'
GREEN='\033[0;32m'
NC='\033[0m'

print_header() {
    echo "============================================================================"
    echo "   T-STD v3.5 Deterministic Shaper Validation (Duration: ${DUR}s)"
    echo "============================================================================"
    printf "%4s | %6s | %6s | %6s | %6s | %10s | %10s | %s\n" \
           "MODE" "MEANk" "±DEVk" "STDk" "SCORE" "P-JIT" "PKTS" "STATUS"
    echo "----------------------------------------------------------------------------"
}

run_test() {
    local mode=$1
    local muxrate_val=1200
    local dst="${OUT_DIR}/v35_test_m${mode}.ts"

    # 1. 运行 FFmpeg
    $ffm -hide_banner -y -i "$SRC" -t "$DUR" \
        -c:v libwz264 -b:v "$VBR_TARGET" -preset fast \
        -muxrate "$MUX_TARGET" -mpegts_tstd_mode "$mode" \
        -mpegts_tstd_debug 2 -mpegts_start_pid 0x21 \
        "$dst" > "${dst}.log" 2>&1

    # 2. 调用高精度审计器
    local audit=$(python3 "$AUDITOR" "$dst" --vid_pid 0x21 --muxrate $((muxrate_val * 1000)))
    read mean max min dev std score pcr_jit pkts <<< $audit

    # 3. 严格判定逻辑
    local status="${GREEN}PASS${NC}"

    # 检查是否产出数据 (Stall Check)
    if [ -z "$pkts" ] || [ "$pkts" -lt 500 ]; then
        status="${RED}FAIL (STALL)${NC}"
        score="N/A"
    else
        # 审计 Score < 30 为广播级优等, < 50 为工业级合格
        if (( $(echo "$score > 50.0" | bc -l) )); then
            status="${RED}FAIL (FLUCT)${NC}"
        fi
        if (( $(echo "$pcr_jit > 500.0" | bc -l) )); then
            status="${RED}FAIL (P-JIT)${NC}"
        fi
    fi

    printf "%4s | %6s | %6s | %6s | %6s | %10s | %10s | %b\n" \
           "$mode" "$mean" "±$dev" "$std" "$score" "${pcr_jit}ns" "${pkts:-0}" "$status"
}

rm -f ${OUT_DIR}/*.ts ${OUT_DIR}/*.log
print_header
run_test 1
run_test 2
echo "----------------------------------------------------------------------------"
