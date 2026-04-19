#!/bin/bash
# T-STD 8-Hour Overnight Stress Test (UDP CBR 800k/1200k)
# Goal: Continuous 8h run with physical layer pacing and real-time TSDuck audit.

# --- Environment Fix for TSDuck ---
export TSPLUGINS_PATH="/usr/lib/tsduck:/usr/local/lib/tsduck"
export LD_LIBRARY_PATH="$LD_LIBRARY_PATH:/usr/lib/tsduck:/usr/local/lib/tsduck"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
OUT_DIR="${ROOT_DIR}/output/overnight_stress"
mkdir -p "$OUT_DIR"

FFMPEG_BIN="${ROOT_DIR}/../ffmpeg.wz.master/ffdeps_img/ffmpeg/bin/ffmpeg"
SRC="/home/lmwang/dev/cae/sample/knet_sd_03.ts"
DUR_SEC=$((8 * 3600))
UDP_PORT="12350"
TARGET_VBR="800"
TARGET_MUX="1200"

export WZ_LICENSE_KEY="/home/lmwang/dev/cae/wz_license.key"

FFM_LOG="${OUT_DIR}/ffmpeg.log"
TSD_LOG="${OUT_DIR}/tsduck_live.log"
CAP_TS="${OUT_DIR}/stress_stream.ts"

echo "=========================================================="
echo "   T-STD 8-HOUR STRESS HARNESS: UDP CBR 800K/1200K"
echo "=========================================================="

# 1. Kill potentially stuck processes
fuser -k ${UDP_PORT}/udp 2>/dev/null
rm -f "$FFM_LOG" "$TSD_LOG" "$CAP_TS"

# 2. Launch Pipeline
echo "[*] Starting FFmpeg (Pacing) -> UDP (Bitrate Control) -> TSDuck (Audit)"

# FFmpeg Pusher
# NOTE: ?bitrate= is CRITICAL for physical layer CBR over UDP.
nohup $FFMPEG_BIN -hide_banner -y -stream_loop -1 -thread_queue_size 512 -rw_timeout 30000000 -fflags +discardcorrupt -re \
    -i "$SRC" -t "$DUR_SEC" \
    -metadata comment=wzcaetrans \
    -filter_complex "[0:v]fps=fps=25[fg_0_fps];[fg_0_fps]wzaipreopt=enhtype=WZ_FaceMask_MNN:expandRatio=0.1:speedLvlFace=1,wzoptimize=autoenh=0:ynslvl=0:uvnslvl=0:uvenh=0:sharptype=3:yenh=1.6:thrnum=2[fg_0_custom]" \
    -map "[fg_0_custom]" -c:v:0 libwz264 -g:v:0 25 -force_key_frames:v:0 "expr:if(mod(n,25),0,1)" \
    -preset:v:0 fast -wz264-params:v:0 "keyint=25:min-keyint=25:aq-mode=2:aq-weight=0.4:aq-strength=1.0:aq-smooth=1.0:psy-rd=0.3:psy-rd-roi=0.4:qcomp=0.65:rc-lookahead=10:pbratio=1.1:vbv-maxrate=${TARGET_VBR}:vbv-bufsize=${TARGET_VBR}:nal-hrd=cbr:force-cfr=1:aud=1:scenecut=0:b-adapt=0" \
    -map 0:a -c:a:0 copy -map "0:d?" -c:d copy -pes_payload_size 0 -threads 2 -pix_fmt yuv420p -color_range tv \
    -b:v ${TARGET_VBR}k -flush_packets 0 -muxrate ${TARGET_MUX}k -inputbw 0 -oheadbw 25 \
    -maxbw 0 -latency 1000000 -muxdelay 0.9 -pcr_period 30 -pat_period 0.2 -sdt_period 0.25 \
    -mpegts_start_pid 0x21 -mpegts_pcr_pid 0x21 -mpegts_tstd_mode 1 -mpegts_tstd_debug 2 \
    -max_muxing_queue_size 4096 -max_interleave_delta 30000000 \
    -f mpegts "udp://127.0.0.1:$UDP_PORT?bitrate=1200000&pkt_size=1316" > "$FFM_LOG" 2>&1 &
FFM_PID=$!

# TSDuck Sentinel
nohup tsp -v -I ip "$UDP_PORT" \
    -P bitrate_monitor -p 5 -t 5 --max 1250000 --min 1150000 \
    -P pcrverify \
    -P continuity \
    -O file "$CAP_TS" > "$TSD_LOG" 2>&1 &
TSD_PID=$!

# 3. Final Survival Check
sleep 15
if ps -p $FFM_PID > /dev/null && ps -p $TSD_PID > /dev/null; then
    echo -e "\033[32m[PASS] OVERNIGHT 800K/1200K PIPELINE IS ACTIVE.\033[0m"
    echo "FFmpeg PID: $FFM_PID | TSDuck PID: $TSD_PID"
    echo "Logs: tail -f $FFM_LOG"
else
    echo -e "\033[31m[FAIL] Pipeline failed to start. Check $FFM_LOG\033[0m"
    kill $FFM_PID $TSD_PID 2>/dev/null
    exit 1
fi
echo "=========================================================="
