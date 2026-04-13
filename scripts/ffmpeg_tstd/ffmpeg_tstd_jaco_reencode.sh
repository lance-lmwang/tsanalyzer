#!/bin/bash
# T-STD Jaco Final Verification
# Purpose: Test 8-hour jump handling using the full original file with -copyts.

ROOT_DIR=$(dirname $(dirname $(dirname $(readlink -f $0))))
OUT_DIR="${ROOT_DIR}/output"
mkdir -p "$OUT_DIR"

ffm="${ROOT_DIR}/../ffmpeg.wz.master/ffdeps_img/ffmpeg/bin/ffmpeg"
src="/home/lmwang/sample/jaco/202508300200_Al-Taawoun_VS_Al-Nassr_2_cut_400M.ts"

echo "=== Jaco Jump Test: T-STD with CBR Re-encode ==="

# --- Test 1: T-STD Mode (On) ---
echo "[Test 1] T-STD Muxer (Re-encoding)..."
dst="${OUT_DIR}/jaco_tstd.ts"
log_file="${OUT_DIR}/jaco_tstd.log"

base_params="-hide_banner -v error -re -y -thread_queue_size 128 -rw_timeout 30000000 -fflags +discardcorrupt -i '$src' \
      -metadata comment=wzcaetrans \
      -filter_complex '[0:v]fps=fps=25[fg_0_fps];[fg_0_fps]wzaipreopt=enhtype=WZ_FaceMask_MNN:expandRatio=0.1:speedLvlFace=1,wzoptimize=autoenh=0:ynslvl=0:uvnslvl=0:uvenh=0:sharptype=3:yenh=1.6:thrnum=2[fg_0_custom]' \
      -map [fg_0_custom] -c:v:0 libwz264 -force_key_frames:v:0 'expr:eq(mod(n,25),0)' \
      -preset:v:0 fast -wz264-params:v:0 'keyint=25:min-keyint=25:aq-mode=2:aq-weight=0.4:aq-strength=1.0:aq-smooth=1.0:psy-rd=0.3:psy-rd-roi=0.4:qcomp=0.65:rc-lookahead=10:pbratio=1.1:vbv-maxrate=1600:vbv-bufsize=1600:nal-hrd=cbr:force-cfr=1:aud=1:scenecut=0:b-adapt=0' \
      -map 0:a -c:a:0 copy -map 0:d? -c:d copy -pes_payload_size 0 -threads 2 -pix_fmt yuv420p -color_range tv \
      -b:v 1600k -flush_packets 0 -muxrate 2100k -inputbw 0 -oheadbw 25 \
      -maxbw 0 -latency 1000000 -muxdelay 0.9 -pcr_period 30 -pat_period 0.2 -sdt_period 0.25 \
      -mpegts_start_pid 0x21 -max_muxing_queue_size 4096 -max_interleave_delta 3000000"

#cmd_tstd="$ffm $base_params -mpegts_tstd_mode 1 -f mpegts '$dst' > $log_file 2>&1"
cmd_tstd="$ffm $base_params -mpegts_tstd_mode 1 -f mpegts '$dst'"
echo "run: $cmd_tstd"
eval $cmd_tstd

RET_TSTD=$?
SIZE_TSTD=$(stat -c%s "$dst" 2>/dev/null || echo 0)
echo "[Result] T-STD Exit Code: $RET_TSTD, Output Size: $SIZE_TSTD bytes"

echo "------------------------------------------"
echo "=== Final Analysis ==="
grep "Initializing STC anchor" "$log_file"
grep "STC gap > 1s" "$log_file"
grep "T-STD" "$log_file" | tail -n 10

if [ $SIZE_TSTD -gt 0 ]; then
    echo "[*] Analyzing T-STD Output Compliance..."
    ${ROOT_DIR}/scripts/ffmpeg_tstd/ffmpeg_tstd_verify_compliance.sh "$log_file"
fi
