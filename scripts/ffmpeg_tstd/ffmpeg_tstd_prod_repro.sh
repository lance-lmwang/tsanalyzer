#!/bin/bash
# T-STD Production Config Comparative Audit (Low vs High Bitrate)
# Purpose: Determine if T-STD issues are specific to low-bitrate "narrow pipe" scenarios.

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
OUT_DIR="${ROOT_DIR}/output/prod_repro"
mkdir -p "$OUT_DIR"

FFMPEG_ROOT="$(cd "$ROOT_DIR/../ffmpeg.wz.master" && pwd)"
ffm="${FFMPEG_ROOT}/ffdeps_img/ffmpeg/bin/ffmpeg"

SRC="/home/lmwang/dev/cae/sample/knet_sd_03.ts"
[ ! -f "$SRC" ] && SRC="/home/lmwang/dev/cae/sample/af2_srt_src.ts"

run_scenario() {
    local v_br=$1
    local m_br=$2
    local name=$3
    local log="${OUT_DIR}/prod_${name}.log"
    local ts="${OUT_DIR}/prod_${name}.ts"

    echo "[*] Running Scenario: $name ($v_br video / $m_br muxrate)..."

    $ffm -y -hide_banner -v trace \
          -thread_queue_size 128 -rw_timeout 30000000 -fflags +discardcorrupt -re \
          -i "$SRC" \
          -filter_complex "[0:v]fps=fps=25[fg_0_fps];[fg_0_fps]wzoptimize=autoenh=0:sharptype=3:yenh=1.6:thrnum=2[fg_0_custom]" \
          -map "[fg_0_custom]" -c:v:0 libwz264 -g:v:0 25 -force_key_frames:v:0 "expr:if(mod(n,25),0,1)" -preset:v:0 fast \
          -wz264-params "keyint=25:min-keyint=25:aq-mode=2:aq-weight=0.4:aq-strength=1.0:aq-smooth=1.0:psy-rd=0.3:psy-rd-roi=0.4:qcomp=0.65:rc-lookahead=10:pbratio=1.1:vbv-maxrate=${v_br%k}:vbv-bufsize=${v_br%k}:nal-hrd=cbr:force-cfr=1:aud=1:scenecut=0:b-adapt=0" \
          -map 0:a -c:a:0 copy \
          -pes_payload_size 0 -threads 2 -pix_fmt yuv420p -color_range tv -b:v $v_br \
          -mpegts_flags +pat_pmt_at_frames -flush_packets 0 \
          -muxrate $m_br -inputbw 0 -oheadbw 25 -maxbw 0 -latency 1000000 \
          -muxdelay 0.9 -pcr_period 18 -pat_period 0.2 -sdt_period 0.25 \
          -mpegts_start_pid 0x21 -mpegts_tstd_mode 1 \
          -max_muxing_queue_size 4096 -max_interleave_delta 30000000 \
          -t 20 \
          "$ts" > "$log" 2>&1

    echo "[*] Analysis for $name:"
    # 1. 视频波动
    python3 scripts/ffmpeg_tstd/tstd_bitrate_auditor.py --log "$log" --pid 0x0021 --window 1.0 --skip 5.0 | grep "Fluctuation"
    # 2. 音频积压 (PID 34)
    MAX_A_TOK=$(grep "PID:34" "$log" | tail -n 50 | grep "TOK:" | awk -F'TOK:' '{print $2}' | awk '{print $1}' | sort -nr | head -n 1)
    echo "    Audio Peak Tokens: $MAX_A_TOK"
}

# 执行对比
run_scenario "600k" "1100k" "Low_Bitrate"
echo "------------------------------------------------"
run_scenario "1600k" "2000k" "High_Bitrate"
