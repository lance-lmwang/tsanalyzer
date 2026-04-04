#!/bin/bash
# T-STD End-to-End Real-time UDP CBR Verification
# FFmpeg (Real-time) -> UDP Loopback -> TSA Analysis

ROOT_DIR=$(dirname $(dirname $(dirname $(readlink -f $0))))
OUT_DIR="${ROOT_DIR}/output"
mkdir -p "$OUT_DIR"

FFMPEG_BIN="${ROOT_DIR}/../ffmpeg.wz.master/ffdeps_img/ffmpeg/bin/ffmpeg"
FFPROBE_BIN="${ROOT_DIR}/../ffmpeg.wz.master/ffdeps_img/ffmpeg/bin/ffprobe"
TSA_CLI="${ROOT_DIR}/build/tsa_cli"
SRC="${ROOT_DIR}/../sample/input.mp4"
UDP_ADDR="127.0.0.1"
UDP_PORT="1234"
MUXRATE=2600000

# Cleanup on exit
trap "echo '[*] Cleaning up...'; kill $FF_PID $TSA_PID 2>/dev/null; exit" EXIT

echo "[1/3] Starting FFmpeg Real-time Pusher (Loop)..."
# Using -v info to reduce log overhead
$FFMPEG_BIN -y -re -stream_loop -1 -v info -i "$SRC" \
      -map 0:v:0 -map 0:a:0 \
      -c:a libfdk_aac -b:a 64k \
      -vcodec libwz264 \
      -wz264-params bframes=0:keyint=25:vbv-maxrate=2100:vbv-bufsize=2100:nal-hrd=cbr:force-cfr=1:aud=1 \
      -b:v 2300000 -pix_fmt yuv420p \
      -f mpegts -mpegts_flags +pat_pmt_at_frames \
      -muxrate $MUXRATE -muxdelay 0.9 \
      -mpegts_tstd_mode 1 \
      -pcr_period 25 -pat_period 0.1s -sdt_period 0.25s \
      "udp://$UDP_ADDR:$UDP_PORT?pkt_size=1316&bitrate=$MUXRATE" > "${OUT_DIR}/ffmpeg_live.log" 2>&1 &
FF_PID=$!

sleep 3 # Wait for stream to stabilize

echo "[2/3] Verifying stream availability via ffprobe..."
$FFPROBE_BIN -v error -show_format -show_streams "udp://$UDP_ADDR:$UDP_PORT"
if [ $? -ne 0 ]; then
    echo "[ERROR] Cannot pull stream from UDP. Check ${OUT_DIR}/ffmpeg_live.log"
    exit 1
fi
echo "[SUCCESS] Stream is alive."

echo "[3/3] Launching TSA Real-time Analysis (30 seconds)..."
echo "Press Ctrl+C to stop early."
# Start TSA_CLI in live mode
$TSA_CLI -u $UDP_PORT -v INFO &
TSA_PID=$!

# Run for 30 seconds
sleep 30

echo "[*] Test completed successfully."
