#!/bin/bash
# T-STD Real-time UDP CBR Stability Test

ROOT_DIR=$(dirname $(dirname $(dirname $(readlink -f $0))))
FFMPEG_BIN="${ROOT_DIR}/../ffmpeg.wz.master/ffdeps_img/ffmpeg/bin/ffmpeg"
SRC="${ROOT_DIR}/../sample/input.mp4"

bitrate_kb=2100
bitrate=2300000
muxrate=2600000
udp_dst="udp://127.0.0.1:1234?pkt_size=1316&bitrate=$muxrate&fifo_size=1000000"

echo "[*] Starting FFmpeg Real-time Stream (UDP Pushing)..."
cmd="$FFMPEG_BIN -y -re -stream_loop -1 -v error -i '$SRC' \
      -map 0:v:0 -map 0:a:0 \
      -c:a libfdk_aac -profile:a aac_low -b:a 64k \
      -vf yadif=mode=0:parity=auto:deint=1,scale=1920:1080,fps=fps=25 \
      -vcodec libwz264 \
      -wz264-params bframes=0:keyint=25:vbv-maxrate=$bitrate_kb:vbv-bufsize=$bitrate_kb:nal-hrd=cbr:force-cfr=1:aud=1 -threads 6 \
      -b:v $bitrate -profile:v Main -preset medium -pix_fmt yuv420p \
      -force_key_frames 'expr:gte(t,n_forced*1)' \
      -dn \
      -flush_packets 0 \
      -f mpegts -mpegts_flags +pat_pmt_at_frames \
      -muxrate $muxrate -muxdelay 0.9 \
      -mpegts_tstd_mode 1 \
      -pcr_period 25 -pat_period 0.2 -sdt_period 0.25 \
      '$udp_dst'"

timeout 15 bash -c "$cmd"
ret=$?

if [ $ret -eq 124 ]; then
    echo "[SUCCESS] Real-time UDP test completed 15s without crashing."
else
    echo "[ERROR] FFmpeg exited prematurely with code $ret"
fi
