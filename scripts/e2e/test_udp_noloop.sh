#!/bin/bash
rm -f test_30s.ts udp_capture_dump.ts capture.log pusher.log
FFMPEG_BIN="../../ffmpeg.wz.master/ffdeps_img/ffmpeg/bin/ffmpeg"

echo "[*] Phase 1: Generating standard 30s TS file..."
$FFMPEG_BIN -y -v warning -stream_loop -1 -i ../../sample/input.mp4 -t 30 -c:v libwz264 -b:v 1600k -c:a libfdk_aac -b:a 64k -f mpegts test_30s.ts

echo "[*] Phase 2: Starting UDP Receiver..."
$FFMPEG_BIN -i "udp://127.0.0.1:1234" -c copy -y udp_capture_dump.ts > capture.log 2>&1 < /dev/null &
CAPTURE_PID=$!
sleep 2

echo "[*] Phase 3: Starting Real-time UDP Pusher (No Loop)..."
$FFMPEG_BIN -re -i test_30s.ts -c copy -f mpegts -muxrate 2000000 -mpegts_tstd_mode 1 "udp://127.0.0.1:1234?pkt_size=1316" > pusher.log 2>&1

echo "[*] Pusher finished. Stopping receiver..."
kill -2 $CAPTURE_PID
wait $CAPTURE_PID 2>/dev/null || true

ls -lh udp_capture_dump.ts
echo "[*] Phase 4: Analyzing captured stream..."
./tools/tsa_cli udp_capture_dump.ts --metrology --industrial
