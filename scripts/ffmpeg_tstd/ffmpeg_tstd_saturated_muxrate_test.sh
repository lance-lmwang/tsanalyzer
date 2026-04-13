#!/bin/bash
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
OUT_DIR="${ROOT_DIR}/output"
mkdir -p "$OUT_DIR"

FFMPEG_ROOT="$(cd "$ROOT_DIR/../ffmpeg.wz.master" && pwd)"
ffm="${FFMPEG_ROOT}/ffdeps_img/ffmpeg/bin/ffmpeg"
src="/home/lmwang/dev/cae/sample/input.mp4"
src="${ROOT_DIR}/../sample//knet_sd_03.ts"
src="${ROOT_DIR}/../sample/af2_srt_src.ts"

echo "[*] Source: $src"
# Base command parameters
base_params="-hide_banner -y -thread_queue_size 128 -rw_timeout 30000000 -fflags +discardcorrupt -i '$src' \
      -metadata comment=wzcaetrans \
      -filter_complex '[0:v]fps=fps=25[fg_0_fps];[fg_0_fps]wzaipreopt=enhtype=WZ_FaceMask_MNN:expandRatio=0.1:speedLvlFace=1,wzoptimize=autoenh=0:ynslvl=0:uvnslvl=0:uvenh=0:sharptype=3:yenh=1.6:thrnum=2[fg_0_custom]' \
      -map [fg_0_custom] -c:v:0 libwz264 -force_key_frames:v:0 'expr:eq(mod(n,25),0)' \
      -preset:v:0 fast -wz264-params:v:0 'keyint=25:min-keyint=25:aq-mode=2:aq-weight=0.4:aq-strength=1.0:aq-smooth=1.0:psy-rd=0.3:psy-rd-roi=0.4:qcomp=0.65:rc-lookahead=10:pbratio=1.1:vbv-maxrate=600:vbv-bufsize=600:nal-hrd=cbr:force-cfr=1:aud=1:scenecut=0:b-adapt=0' \
      -map 0:a -c:a:0 copy -map 0:d? -c:d copy -pes_payload_size 0 -threads 2 -pix_fmt yuv420p -color_range tv \
      -b:v 600k -flush_packets 0 -muxrate 1300k -inputbw 0 -oheadbw 25 \
      -maxbw 0 -latency 1000000 -muxdelay 0.9 -pcr_period 18 -pat_period 0.2 -sdt_period 0.25 \
      -mpegts_start_pid 0x21 -max_muxing_queue_size 4096 -max_interleave_delta 30000000"

# --- RUN 2: T-STD Muxer mode 1 ---
dst_tstd="${OUT_DIR}/live_mode1.ts"
log_tstd="${OUT_DIR}/live_mode1.log"
cmd_tstd="$ffm $base_params -mpegts_tstd_mode 1 -f mpegts '$dst_tstd' > $log_tstd 2>&1"

echo "run: $cmd_tstd"
eval $cmd_tstd
if [ $? -eq 0 ]; then echo "[SUCCESS] T-STD run finished."; else echo "[FAILED] T-STD run failed."; fi

ls -l $dst_tstd
cp -f $dst_tstd ~/bj_data3/lmwang/output/
grep -E "DROP|FUSE|CRITICAL|WARNING|ERROR|skew" $log_tstd

# --- RUN 2: T-STD Muxer mode 2 ---
dst_tstd="${OUT_DIR}/live1_mode2.ts"
log_tstd="${OUT_DIR}/live1_mode2.log"
cmd_tstd="$ffm $base_params -mpegts_tstd_mode 2 -f mpegts '$dst_tstd' > $log_tstd 2>&1"

echo "run: $cmd_tstd"
eval $cmd_tstd
if [ $? -eq 0 ]; then echo "[SUCCESS] T-STD run finished."; else echo "[FAILED] T-STD run failed."; fi

ls -l $dst_tstd
cp -f $dst_tstd ~/bj_data3/lmwang/output/
grep -E "DROP|FUSE|CRITICAL|WARNING|ERROR|skew" $log_tstd
