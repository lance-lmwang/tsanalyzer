#!/bin/bash
# T-STD End-to-End Real-time UDP CBR Verification (Professional Grade)
# FFmpeg (T-STD Engine) -> UDP Port -> nc (Bit-accurate Capture) -> TSA Analysis
# UPDATED: Using production parameters that triggered the coredump bug (NOW FIXED).

ROOT_DIR=$(dirname $(dirname $(dirname $(readlink -f $0))))
OUT_DIR="${ROOT_DIR}/output"
mkdir -p "$OUT_DIR"

FFMPEG_BIN="${ROOT_DIR}/../ffmpeg.wz.master/ffdeps_img/ffmpeg/bin/ffmpeg"
FFPROBE_BIN="${ROOT_DIR}/../ffmpeg.wz.master/ffdeps_img/ffmpeg/bin/ffprobe"
VERIFY_SCRIPT="${ROOT_DIR}/scripts/ffmpeg_tstd/ffmpeg_tstd_verify_compliance.sh"
SRC="${ROOT_DIR}/../sample/af2_srt_src.ts"
src="${ROOT_DIR}/../sample//knet_sd_03.ts"
UDP_ADDR="127.0.0.1"
UDP_PORT="12345"
CAPTURE_FILE="${OUT_DIR}/udp_capture.ts"
LOG_FILE="${OUT_DIR}/udp_capture.log"
TEST_DURATION=1200

# 1. Environment Guard
echo "[*] Cleaning up environment..."
fuser -k ${UDP_PORT}/udp 2>/dev/null
rm -f "$CAPTURE_FILE" "$LOG_FILE"

# 2. Start nc Receiver (Background)
echo "[1/4] Starting bit-accurate UDP receiver (nc) on port $UDP_PORT..."
# Using raw nc to ensure zero packet metadata overhead
nc -u -l -p $UDP_PORT > "$CAPTURE_FILE" &
NC_PID=$!

# 3. Start FFmpeg Pusher
echo "[2/4] Pushing T-STD stream for ${TEST_DURATION}s using production parameters..."
# Updated parameters: Including wzaipreopt, wzoptimize and strict mpegts_tstd pacing.
$FFMPEG_BIN -hide_banner -stream_loop -1 -y -v trace -thread_queue_size 128 -rw_timeout 30000000 -fflags +discardcorrupt -re -i "$SRC" \
      -t $TEST_DURATION \
      -metadata comment=wzcaetrans \
      -filter_complex '[0:v]fps=fps=25[fg_0_fps];[fg_0_fps]wzaipreopt=enhtype=WZ_FaceMask_MNN:expandRatio=0.1:speedLvlFace=1,wzoptimize=autoenh=0:ynslvl=0:uvnslvl=0:uvenh=0:sharptype=3:yenh=1.6:thrnum=2[fg_0_custom]' \
      -map [fg_0_custom] -c:v:0 libwz264 -g:v:0 25 -force_key_frames:v:0 'expr:if(mod(n,25),0,1)' \
      -preset:v:0 fast -wz264-params:v:0 'keyint=25:min-keyint=25:aq-mode=2:aq-weight=0.4:aq-strength=1.0:aq-smooth=1.0:psy-rd=0.3:psy-rd-roi=0.4:qcomp=0.65:rc-lookahead=10:pbratio=1.1:vbv-maxrate=600:vbv-bufsize=600:nal-hrd=cbr:force-cfr=1:aud=1:scenecut=0:b-adapt=0' \
      -map 0:a -c:a:0 copy -map 0:d? -c:d copy -pes_payload_size 0 -threads 2 -pix_fmt yuv420p -color_range tv \
      -b:v 600k -mpegts_flags +pat_pmt_at_frames -flush_packets 0 -muxrate 1300k -inputbw 0 -oheadbw 25 \
      -maxbw 0 -latency 1000000 -muxdelay 0.9 -pcr_period 18 -pat_period 0.2 -sdt_period 0.25 \
      -mpegts_start_pid 0x21 -mpegts_tstd_mode 1 -max_muxing_queue_size 4096 -max_interleave_delta 30000000 \
      -f mpegts "udp://$UDP_ADDR:$UDP_PORT?pkt_size=1316" > "$LOG_FILE" 2>&1

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

echo "[5/5] Running Industrial-grade TSDuck PCR Audit..."
tsp -I file "$CAPTURE_FILE" -P analyze -P pcrverify -P continuity -O drop

echo ""
echo "[*] UDP End-to-End Test finished."
