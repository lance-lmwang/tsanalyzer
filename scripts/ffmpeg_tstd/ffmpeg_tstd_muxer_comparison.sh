#!/bin/bash
# T-STD vs Legacy Muxer Comparison Script
# Purpose: Compare outputs between legacy muxer and T-STD engine using production parameters.

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
OUT_DIR="${ROOT_DIR}/output"
mkdir -p "$OUT_DIR"

FFMPEG_ROOT="$(cd "$ROOT_DIR/../ffmpeg.wz.master" && pwd)"
ffm="${FFMPEG_ROOT}/ffdeps_img/ffmpeg/bin/ffmpeg"
src="/home/lmwang/dev/cae/sample/input.mp4"
src="/home/lmwang/dev/cae/sample/knet_sd_03.ts"
src="${ROOT_DIR}/../sample/af2_srt_src.ts"

echo "[*] Starting Dual Muxer Comparison Test (Full File)..."
echo "[*] Source: $src"

# Base command parameters
base_params="-hide_banner -y -v trace -re -thread_queue_size 128 -rw_timeout 30000000 -fflags +discardcorrupt -i '$src' \
      -metadata comment=wzcaetrans \
      -filter_complex '[0:v]fps=fps=25[fg_0_fps];[fg_0_fps]wzaipreopt=enhtype=WZ_FaceMask_MNN:expandRatio=0.1:speedLvlFace=1,wzoptimize=autoenh=0:ynslvl=0:uvnslvl=0:uvenh=0:sharptype=3:yenh=1.6:thrnum=2[fg_0_custom]' \
      -map [fg_0_custom] -c:v:0 libwz264 -g:v:0 25 -force_key_frames:v:0 'expr:if(mod(n,25),0,1)' \
      -preset:v:0 fast -wz264-params:v:0 'keyint=25:min-keyint=25:aq-mode=2:aq-weight=0.4:aq-strength=1.0:aq-smooth=1.0:psy-rd=0.3:psy-rd-roi=0.4:qcomp=0.65:rc-lookahead=10:pbratio=1.1:vbv-maxrate=600:vbv-bufsize=600:nal-hrd=cbr:force-cfr=1:aud=1:scenecut=0:b-adapt=0' \
      -map 0:a -c:a:0 copy -map 0:d? -c:d copy -pes_payload_size 0 -threads 2 -pix_fmt yuv420p -color_range tv \
      -b:v 600k -mpegts_flags +pat_pmt_at_frames -flush_packets 0 -muxrate 1100k -inputbw 0 -oheadbw 25 \
      -maxbw 0 -latency 1000000 -muxdelay 0.5 -pcr_period 18 -pat_period 0.2 -sdt_period 0.25 \
      -mpegts_start_pid 0x21 -max_muxing_queue_size 4096 -max_interleave_delta 30000000"

# --- RUN 2: T-STD Muxer ---
dst_tstd="${OUT_DIR}/compare_tstd.ts"
log_tstd="${OUT_DIR}/compare_tstd.log"
echo ""
echo "========================================="
echo "[2/2] Executing T-STD Muxer (T-STD ON)"
cmd_tstd="$ffm $base_params -mpegts_tstd_mode 1 -f mpegts '$dst_tstd' > $log_tstd 2>&1"
eval $cmd_tstd
if [ $? -eq 0 ]; then echo "[SUCCESS] T-STD run finished."; else echo "[FAILED] T-STD run failed."; fi


# --- RUN 1: Legacy Muxer ---
dst_legacy="${OUT_DIR}/compare_legacy.ts"
log_legacy="${OUT_DIR}/compare_legacy.log"
echo ""
echo "========================================="
echo "[1/2] Executing Legacy Muxer (T-STD OFF)"
cmd_legacy="$ffm $base_params -mpegts_tstd_mode 0 -f mpegts '$dst_legacy' > $log_legacy 2>&1"
eval $cmd_legacy
if [ $? -eq 0 ]; then echo "[SUCCESS] Legacy run finished."; else echo "[FAILED] Legacy run failed."; fi

# --- COMPARISON ---
echo ""
echo "========================================="
echo "[*] Generating MediaInfo Comparison..."
mediainfo "$dst_legacy" > "${OUT_DIR}/mediainfo_legacy.txt"
mediainfo "$dst_tstd" > "${OUT_DIR}/mediainfo_tstd.txt"
echo "[*] Done. You can find the reports at:"
echo "    - ${OUT_DIR}/mediainfo_legacy.txt"
echo "    - ${OUT_DIR}/mediainfo_tstd.txt"
