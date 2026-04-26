#!/bin/bash
# Integrated T-STD Audit: Internal Telemetry vs PCR Analysis
# Mode 1 Only

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
FFMPEG_ROOT="$(cd "$ROOT_DIR/../ffmpeg.wz.master" && pwd)"
ffm="${FFMPEG_ROOT}/ffdeps_img/ffmpeg/bin/ffmpeg"
AUDITOR="${SCRIPT_DIR}/pcr_sliding_window.py"
SRC="/home/lmwang/dev/cae/sample/knet_sd_03.ts"
[ ! -f "$SRC" ] && SRC="/home/lmwang/dev/cae/sample/af2_srt_src.ts"

echo "================================================================"
echo "  INTEGRATED T-STD AUDIT: LOGS vs. EXTERNAL ANALYSIS (Mode 1)"
echo "================================================================"

# 1. 生产环境全参数转码 (启用 Mode 1)
dst="output_m1_audit.ts"
log="audit_m1.log"
$ffm -hide_banner -y -thread_queue_size 128 -rw_timeout 30000000 -fflags +discardcorrupt \
    -i "$SRC" -t 60 \
    -metadata comment=wzcaetrans \
    -filter_complex '[0:v]fps=fps=25[fg_0_fps];[fg_0_fps]wzaipreopt=enhtype=WZ_FaceMask_MNN:expandRatio=0.1:speedLvlFace=1,wzoptimize=autoenh=0:ynslvl=0:uvnslvl=0:uvenh=0:sharptype=3:yenh=1.6:thrnum=2[fg_0_custom]' \
    -map '[fg_0_custom]' -c:v:0 libwz264 -g:v:0 25 -force_key_frames:v:0 'expr:if(mod(n,25),0,1)' \
    -preset:v:0 fast -wz264-params:v:0 'keyint=25:min-keyint=25:aq-mode=2:aq-weight=0.4:aq-strength=1.0:aq-smooth=1.0:psy-rd=0.3:psy-rd-roi=0.4:qcomp=0.65:rc-lookahead=10:pbratio=1.1:vbv-maxrate=600:vbv-bufsize=600:nal-hrd=cbr:force-cfr=1:aud=1:scenecut=0:b-adapt=0' \
    -map 0:a -c:a:0 copy -map '0:d?' -c:d copy -pes_payload_size 0 -threads 2 -pix_fmt yuv420p -color_range tv \
    -b:v 600k -flush_packets 0 -muxrate 1100k -inputbw 0 -oheadbw 25 \
    -maxbw 0 -latency 1000000 -muxdelay 0.9 -pcr_period 30 -pat_period 0.2 -sdt_period 0.25 \
    -mpegts_start_pid 0x21 -mpegts_tstd_mode 1 -tstd_params "debug=2" -max_muxing_queue_size 4096 -max_interleave_delta 30000000 \
    -f mpegts "$dst" > "$log" 2>&1

# 2. 外部衡量 (Audit)
audit=$(python3 "$AUDITOR" "$dst" --vid_pid 0x21 --pcr_pid 0x21 --window_ms 1504.0 --skip_sec 5.0)
ext_mean=$(echo "$audit" | grep "Mean Bitrate" | awk '{print $3}' | tr -d 'bps')

# 3. 遥感日志对齐衡量
int_mean=$(grep "\[T-STD-AUDIT\]" "$log" | awk -F'WBR:' '{print $2}' | awk '{sum+=$1; n++} END {if (n > 0) printf "%.2f", sum/n; else print 0}')

echo "Payload Mean BR (Audit): $ext_mean_es bps"
echo "Total Bus Mean BR (Telemetry): $int_mean bps"

# 使用 awk 计算效率，处理科学计数法
awk -v payload="$ext_mean_es" -v bus="$int_mean" 'BEGIN {
    if (bus > 0) printf "Efficiency (Payload/Bus): %.2f%%\n", (payload / bus) * 100;
    else print "Diff: N/A";
}'
