#!/bin/bash
rm -f udp_transcode_capture.ts capture_transcode.log pusher_transcode.log
FFMPEG_BIN="../../ffmpeg.wz.master/ffdeps_img/ffmpeg/bin/ffmpeg"
UDP_URL="udp://127.0.0.1:1234?pkt_size=1316"

echo "[*] Phase 1: Starting UDP Receiver..."
$FFMPEG_BIN -i "$UDP_URL" -t 15 -c copy -y udp_transcode_capture.ts > capture_transcode.log 2>&1 < /dev/null &
CAPTURE_PID=$!
sleep 2

echo "[*] Phase 2: Starting Real-time Transcode to UDP (-re, input.mp4)..."
$FFMPEG_BIN -y -re -stream_loop -1 -i ../../sample/input.mp4 -t 15 \
  -c:a libfdk_aac -b:a 64k \
  -c:v libwz264 -b:v 1600k -preset medium -pix_fmt yuv420p \
  -f mpegts -muxrate 2000000 -mpegts_tstd_mode 1 "$UDP_URL" > pusher_transcode.log 2>&1

echo "[*] Phase 3: Analyzing..."
wait $CAPTURE_PID 2>/dev/null || true
ls -lh udp_transcode_capture.ts
./tools/tsa_cli udp_transcode_capture.ts --metrology --industrial
