#!/bin/bash
# 离线 PCR/DTS 闭合审计工具 (Single-Pass Performance Version)
# 用法: ./offline_clock_audit.sh <TS_FILE>

TS_FILE=$1
FFPROBE="./ffdeps_img/ffmpeg/bin/ffprobe"

if [ ! -f "$TS_FILE" ]; then
    echo "Error: File $TS_FILE not found."
    exit 1
fi

echo "[*] Auditing Clock Compliance for: $TS_FILE"

# 1. 提取全流的 DTS (90kHz) 和 PCR (27MHz 原始值)
# 我们只关注视频 PID (通常是 0x21)
# -show_entries packet=dts,pcr 只有在包包含 PCR 时才输出 pcr 字段
$FFPROBE -v error -select_streams v:0 -show_entries packet=dts,pcr -of csv=p=0 "$TS_FILE" > clock_trace.tmp

# 2. 使用 awk 进行时序追踪
# 逻辑：保存最近见到的 PCR，与当前 DTS 对比
awk -F',' '
BEGIN { last_pcr_90k = 0; violations = 0; total_frames = 0 }
{
    dts = $1;
    pcr_raw = $2;

    # 如果该包包含 PCR，更新最近 PCR 记录
    if (pcr_raw != "" && pcr_raw != "N/A") {
        last_pcr_90k = pcr_raw / 300;
    }

    if (dts != "" && dts != "N/A") {
        total_frames++;
        # 核心判定：PCR 绝对不能大于 DTS
        if (last_pcr_90k > dts) {
            printf "\033[31m[ERROR]\033[0m Frame %d: PCR(%d) > DTS(%d) | Delta: %d units\n",
                   total_frames, last_pcr_90k, dts, last_pcr_90k - dts;
            violations++;
        }
    }
}
END {
    if (violations == 0) {
        printf "\033[32m[PASS]\033[0m Total Frames: %d, Clock Synchrony OK.\n", total_frames;
    } else {
        printf "\033[31m[FAIL]\033[0m Detected %d violations in %d frames!\n", violations, total_frames;
    }
}' clock_trace.tmp

rm -f clock_trace.tmp
