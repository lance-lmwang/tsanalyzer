#!/bin/bash
# T-STD Carrier-Grade Physical Pacing Audit
# Goal: Measure pure T-STD pacer response by bypassing encoder mask via '-c copy'.
# Using authoritative libwz264 configuration.

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
FFMPEG_ROOT="$(cd "$ROOT_DIR/../ffmpeg.wz.master" && pwd)"
ffm="${FFMPEG_ROOT}/ffdeps_img/ffmpeg/bin/ffmpeg"
OUT_DIR="${ROOT_DIR}/output/expert_audit"
mkdir -p "$OUT_DIR"

echo "=========================================================="
echo "   T-STD V3 PHYSICAL LAYER DYNAMICS AUDIT (Expert)        "
echo "=========================================================="

# 1. 构造纯净阶跃源 (7s 500k -> 7s 4000k)
echo "[*] Step 1: Synthesizing pure physical step source (14s total)..."
STEP_SRC="${OUT_DIR}/pure_step_src.ts"

# 预生成分片，强制使用 libwz264 标准参数
# Low Segment: 500k
$ffm -y -f lavfi -i "testsrc=size=1920x1080:rate=25" -f lavfi -i "sine=frequency=1000:sample_rate=48000" -t 7 \
     -vcodec libwz264 -b:v 500k \
     -wz264-params "bframes=0:keyint=25:vbv-maxrate=500:vbv-bufsize=250:nal-hrd=cbr:force-cfr=1:aud=1" \
     -c:a mp2 -b:a 256k "/tmp/step_low.ts" >/dev/null 2>&1

# High Segment: 4000k
$ffm -y -f lavfi -i "testsrc=size=1920x1080:rate=25,noise=alls=100" -f lavfi -i "sine=frequency=1000:sample_rate=48000" -t 7 \
     -vcodec libwz264 -b:v 4000k \
     -wz264-params "bframes=0:keyint=25:vbv-maxrate=4000:vbv-bufsize=2000:nal-hrd=cbr:force-cfr=1:aud=1" \
     -c:a mp2 -b:a 256k "/tmp/step_high.ts" >/dev/null 2>&1

# 使用 concat demuxer 合并
echo "file '/tmp/step_low.ts'" > /tmp/list.txt
echo "file '/tmp/step_high.ts'" >> /tmp/list.txt
$ffm -y -f concat -safe 0 -i /tmp/list.txt -c copy "$STEP_SRC" >/dev/null 2>&1

if [ ! -f "$STEP_SRC" ]; then
    echo "[ERROR] Failed to generate $STEP_SRC"
    exit 1
fi

# 2. T-STD 物理层透传测试 (使用 -re 模拟实时速率)
echo "[*] Step 2: Driving T-STD Engine via Real-time Physical Pass-through..."
EXPERT_LOG="${OUT_DIR}/expert_pacing.log"
# 必须显式指定 -muxrate，否则 T-STD 引擎不会启动
$ffm -y -re -i "$STEP_SRC" -c copy \
     -f mpegts -muxrate 5000k -muxdelay 1.0 \
     -mpegts_tstd_mode 1 -mpegts_tstd_debug 2 \
     "${OUT_DIR}/expert_output.ts" > "$EXPERT_LOG" 2>&1

# 3. 深度动力学分析
echo "[*] Step 3: Extracting Pacing Dynamics (Tail Log Analysis)..."
PACES=$(grep "Pace:" "$EXPERT_LOG" | awk -F'Pace:' '{print $2}' | awk '{print $1}')
RATES=$(grep "Out:" "$EXPERT_LOG" | awk -F'Out:' '{print $2}' | awk '{print $1}' | sed 's/k//g')

if [ -z "$RATES" ]; then
    echo "[ERROR] No telemetry data found in log. Check mpegts_tstd_debug level."
    echo "--- Full Log Dump ---"
    cat "$EXPERT_LOG"
    exit 1
fi

echo "----------------------------------------------------------"
echo "ID | PHYSICAL_OUT (kbps) | PACE_MULT | DYNAMICS_ANALYSIS"
echo "----------------------------------------------------------"

SEC=1
for RATE in $RATES; do
    PACE=$(echo "$PACES" | sed -n "${SEC}p")
    printf "%2d | %18.2f | %9.3f\n" "$SEC" "$RATE" "$PACE"
    SEC=$((SEC + 1))
done
echo "----------------------------------------------------------"

# 获取样本量
COUNT=$(echo "$RATES" | wc -l)
if [ "$COUNT" -ge 2 ]; then
    RATE_START=$(echo "$RATES" | head -n 3 | tail -n 1) # Skip initial transients
    RATE_END=$(echo "$RATES" | tail -n 3 | head -n 1)   # Get stable high rate
    PHYSICAL_JUMP=$(echo "$RATE_END - $RATE_START" | bc)
    echo "[*] EXPERT VERDICT: Observed Physical Delta: ${PHYSICAL_JUMP} kbps"
else
    echo "[WARN] Not enough data points for jump analysis."
fi
