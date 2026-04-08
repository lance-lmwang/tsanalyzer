#!/bin/bash
# T-STD Long-Term Stability & Metrology Test Script
# Purpose: Drive the purified V3 T-STD engine with carrier-grade parameters.

ROOT_DIR=$(dirname $(dirname $(dirname $(readlink -f $0))))
OUT_DIR="${ROOT_DIR}/output"
mkdir -p "$OUT_DIR"

# --- Parameters Configuration ---
ffm="${ROOT_DIR}/../ffmpeg.wz.master/ffdeps_img/ffmpeg/bin/ffmpeg"
src="${ROOT_DIR}/../sample/input.mp4"
prog_id=1
bitrate="1600k"
bitrate_kb=1600
muxrate=2000000
dst="${OUT_DIR}/tstd_smoke.ts"
log_file="${OUT_DIR}/tstd_smoke.log"
test_duration=10

echo "[*] Starting T-STD Smoke Test..."
echo "[*] Output directory: $OUT_DIR"
echo "[*] Log file: $log_file"

# --- FFmpeg Command Logic ---
# Optimized with libwz264 CBR settings and T-STD V3 Pacer
cmd="$ffm -y -re -v trace -stream_loop -1 -autorotate 0 -rw_timeout 10000000 -i '$src' \
      -t $test_duration \
      -map 0:v:0 -map 0:a:0 \
      -c:a libfdk_aac -profile:a aac_low -b:a 64k \
      -vf yadif=mode=0:parity=auto:deint=1,scale=1920:1080,fps=fps=25 \
      -vcodec libwz264 \
      -wz264-params bframes=0:keyint=25:vbv-maxrate=$bitrate_kb:vbv-bufsize=$bitrate_kb:nal-hrd=cbr:force-cfr=1:aud=1 -threads 6 \
      -b:v $bitrate -profile:v Main -preset medium -pix_fmt yuv420p \
      -force_key_frames 'expr:gte(t,n_forced*1)' \
      -dn \
      -flush_packets 0 \
      -metadata service_name="wz_tstd" \
      -metadata service_provider="wz" \
      -f mpegts -mpegts_flags +pat_pmt_at_frames \
      -muxrate $muxrate -muxdelay 0.9 \
      -mpegts_tstd_mode 1 \
      '$dst' \
      > $log_file 2>&1"

# Execute
eval $cmd

if [ $? -eq 0 ]; then
    echo "[SUCCESS] Soak test completed. Analyzing results..."
    ${ROOT_DIR}/scripts/ffmpeg_tstd/ffmpeg_tstd_verify_compliance.sh "$log_file"
else
    echo "[ERROR] FFmpeg process crashed or killed. Check $log_file"
    exit 1
fi
