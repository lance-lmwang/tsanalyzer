#!/bin/bash
# T-STD End-to-End Real-time UDP CBR Verification (Professional Grade)
# FFmpeg (T-STD Engine) -> UDP Port -> nc (Bit-accurate Capture) -> TSA Analysis

ROOT_DIR=$(dirname $(dirname $(dirname $(readlink -f $0))))
OUT_DIR="${ROOT_DIR}/output"
mkdir -p "$OUT_DIR"

FFMPEG_BIN="${ROOT_DIR}/../ffmpeg.wz.master/ffdeps_img/ffmpeg/bin/ffmpeg"
FFPROBE_BIN="${ROOT_DIR}/../ffmpeg.wz.master/ffdeps_img/ffmpeg/bin/ffprobe"
VERIFY_SCRIPT="${ROOT_DIR}/scripts/ffmpeg_tstd/ffmpeg_tstd_verify_compliance.sh"
SRC="${ROOT_DIR}/../sample/input.mp4"
UDP_ADDR="127.0.0.1"
UDP_PORT="12345"
CAPTURE_FILE="${OUT_DIR}/udp_capture.ts"
LOG_FILE="${OUT_DIR}/udp_capture.log"
TEST_DURATION=15

# 1. Environment Guard
echo "[*] Cleaning up environment..."
fuser -k ${UDP_PORT}/udp 2>/dev/null
rm -f "$CAPTURE_FILE" "$LOG_FILE"

# 2. Start nc Receiver (Background)
echo "[1/4] Starting bit-accurate UDP receiver (nc) on port $UDP_PORT..."
nc -u -l -p $UDP_PORT > "$CAPTURE_FILE" &
NC_PID=$!

# 3. Start FFmpeg Pusher
echo "[2/4] Pushing T-STD stream for ${TEST_DURATION}s..."
# Note: Use -v debug to provide telemetry for the compliance script
$FFMPEG_BIN -y -re -v debug -i "$SRC" \
      -t $TEST_DURATION \
      -map 0:v:0 -map 0:a:0 \
      -c:a libfdk_aac -b:a 64k \
      -vcodec libwz264 \
      -wz264-params bframes=0:keyint=25:vbv-maxrate=1600:vbv-bufsize=1600:nal-hrd=cbr:force-cfr=1:aud=1 \
      -b:v 1600k -pix_fmt yuv420p \
      -f mpegts -mpegts_flags +pat_pmt_at_frames \
      -muxrate 2000000 -muxdelay 0.9 \
      -mpegts_tstd_mode 1 \
      "udp://$UDP_ADDR:$UDP_PORT?pkt_size=1316" > "$LOG_FILE" 2>&1

# Wait for nc to finish writing and kill it
sleep 2
kill $NC_PID 2>/dev/null || true

# 4. Post-Mortem Audit
echo "[3/4] Verifying bitstream integrity via ffprobe..."
$FFPROBE_BIN -i "$CAPTURE_FILE" -hide_banner
if [ $? -ne 0 ]; then
    echo "[ERROR] Captured bitstream is invalid or empty. Check $LOG_FILE"
    exit 1
fi

echo "[4/4] Running Deep Metrology Audit..."
"$VERIFY_SCRIPT" "$LOG_FILE"

echo ""
echo "[*] UDP End-to-End Test finished."
