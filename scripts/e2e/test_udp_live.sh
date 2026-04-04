#!/bin/bash
ffm="../../ffmpeg.wz.master/ffdeps_img/ffmpeg/bin/ffmpeg"
src="../../sample/input.mp4"
bitrate="1600k"
bitrate_kb=1600
muxrate=2000000
dst="udp://127.0.0.1:1234?pkt_size=1316"
capture_file="udp_capture_live.ts"

rm -f $capture_file capture_live.log pusher_live.log

echo "[*] Phase 1: Starting UDP Receiver..."
$ffm -i "$dst" -t 30 -c copy -y $capture_file > capture_live.log 2>&1 < /dev/null &
CAPTURE_PID=$!
sleep 2

echo "[*] Phase 2: Starting T-STD Transcode to UDP (30s)..."
$ffm -y -re -v debug -autorotate 0 -rw_timeout 10000000 -i "$src" \
      -t 30 \
      -map 0:v:0 -map 0:a:0 \
      -c:a libfdk_aac -profile:a aac_low -b:a 64k \
      -vf yadif=mode=0:parity=auto:deint=1,scale=1920:1080,fps=fps=25 \
      -vcodec libwz264 \
      -wz264-params bframes=0:keyint=25:vbv-maxrate=$bitrate_kb:vbv-bufsize=$bitrate_kb:nal-hrd=cbr:force-cfr=1:aud=1 -threads 6 \
      -b:v $bitrate -profile:v Main -preset medium -pix_fmt yuv420p \
      -dn \
      -flush_packets 0 \
      -f mpegts -mpegts_flags +pat_pmt_at_frames \
      -muxrate $muxrate -muxdelay 0.9 \
      -mpegts_tstd_mode 1 \
      -mpegts_tstd_token_floor -1024 \
      "$dst" > pusher_live.log 2>&1

echo "[*] Pusher finished. Stopping receiver..."
kill -2 $CAPTURE_PID 2>/dev/null || true
wait $CAPTURE_PID 2>/dev/null || true

ls -lh $capture_file
echo "[*] Phase 3: Analyzing captured UDP stream..."
./tools/tsa_cli $capture_file --metrology --industrial
