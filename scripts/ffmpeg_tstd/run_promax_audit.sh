#!/bin/bash
FFMPEG_ROOT="/home/lmwang/dev/cae/ffmpeg.wz.master"
ffm="${FFMPEG_ROOT}/ffdeps_img/ffmpeg/bin/ffmpeg"
SRC="/home/lmwang/dev/cae/sample/af2_srt_src.ts"
DST="/home/lmwang/dev/cae/tsanalyzer/output/prod_baseline.ts"
AUDITOR="/home/lmwang/dev/cae/tsanalyzer/scripts/ffmpeg_tstd/promax_ts_auditor.py"

echo "=========================================================="
echo "   1. RUNNING PRODUCTION ENCODE (Unmodified T-STD)"
echo "=========================================================="
# 使用您提供的完全一致的生产参数
$ffm -hide_banner -y -thread_queue_size 128 -rw_timeout 30000000 -fflags +discardcorrupt -i "$SRC" \
      -filter_complex '[0:v]fps=fps=25[fg_0_fps];[fg_0_fps]wzaipreopt=enhtype=WZ_FaceMask_MNN:expandRatio=0.1:speedLvlFace=1,wzoptimize=autoenh=0:ynslvl=0:uvnslvl=0:uvenh=0:sharptype=3:yenh=1.6:thrnum=2[fg_0_custom]' \
      -map [fg_0_custom] -c:v:0 libwz264 -g:v:0 25 -force_key_frames:v:0 'expr:if(mod(n,25),0,1)' -preset:v:0 fast \
      -wz264-params "keyint=25:min-keyint=25:aq-mode=2:aq-weight=0.4:aq-strength=1.0:aq-smooth=1.0:psy-rd=0.3:psy-rd-roi=0.4:qcomp=0.65:rc-lookahead=10:pbratio=1.1:vbv-maxrate=600:vbv-bufsize=600:nal-hrd=cbr:force-cfr=1:aud=1:scenecut=0:b-adapt=0" \
      -map 0:a -c:a:0 copy -map 0:d? -c:d copy -pes_payload_size 0 -threads 2 -pix_fmt yuv420p -color_range tv \
      -b:v 600k -flush_packets 0 -muxrate 1100k -inputbw 0 -oheadbw 25 -maxbw 0 -latency 1000000 \
      -muxdelay 0.9 -pcr_period 30 -pat_period 0.2 -sdt_period 0.25 -mpegts_start_pid 0x21 \
      -mpegts_tstd_mode 1 -max_muxing_queue_size 4096 -max_interleave_delta 30000000 \
      -t 30 -f mpegts "$DST" > /dev/null 2>&1

echo "=========================================================="
echo "   2. EXECUTING PROMAX-ALIGNED AUDIT (1504ms window)"
echo "=========================================================="
chmod +x "$AUDITOR"
python3 "$AUDITOR" "$DST" --pid 0x21 --muxrate 1100000 --window_ms 1504.0 --skip_sec 5.0
