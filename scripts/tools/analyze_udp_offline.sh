#!/bin/bash
# analyze_udp_offline.sh - T-STD Physical Integrity Verifier
# Usage: ./scripts/analyze_udp_offline.sh [duration_sec]

DURATION=${1:-10}
FFMPEG_BIN="../../ffmpeg.wz.master/ffdeps_img/ffmpeg/bin/ffmpeg"
UDP_URL="udp://127.0.0.1:1234?pkt_size=1316"
CAPTURE_FILE="udp_capture_dump.ts"
SAMPLE_FILE="live_sample.ts"

echo "[*] Initializing Physical Integrity Test (V7.6)..."
rm -f $CAPTURE_FILE

# 1. Start Capture in background FIRST
echo "[*] Phase 1: Starting UDP Receiver..."
$FFMPEG_BIN -i "$UDP_URL" -t $DURATION -c copy -y $CAPTURE_FILE > capture.log 2>&1 &
CAPTURE_PID=$!

sleep 1

# 2. Start Pusher
echo "[*] Phase 2: Starting Real-time UDP Pusher ($DURATION seconds)..."
$FFMPEG_BIN -re -stream_loop -1 -i $SAMPLE_FILE -c copy -f mpegts -muxrate 2000000 -mpegts_tstd_mode 1 "$UDP_URL" > pusher.log 2>&1 &
PUSHER_PID=$!

# 3. Wait for Capture to finish
echo "[*] Phase 3: Waiting for capture to complete..."
wait $CAPTURE_PID

echo "[*] Phase 4: Cleaning up..."
kill $PUSHER_PID > /dev/null 2>&1
wait $PUSHER_PID > /dev/null 2>&1

# 4. Offline Analysis
if [ -f "$CAPTURE_FILE" ]; then
    echo "[*] Phase 4: Performing Deep Metrology on captured stream..."
    ./tools/tsa_cli $CAPTURE_FILE --metrology --industrial
else
    echo "[ERROR] Capture failed. Check capture.log"
    exit 1
fi
