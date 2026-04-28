#!/bin/bash
# T-STD Long-term UDP Stability & TSDuck Audit
# FFmpeg (T-STD) -> UDP -> Capture -> TSDuck Analysis

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
OUT_DIR="${ROOT_DIR}/output/live_stress"
mkdir -p "$OUT_DIR"

FFMPEG_BIN="${ROOT_DIR}/../ffmpeg.wz.master/ffdeps_img/ffmpeg/bin/ffmpeg"
FFPROBE_BIN="${ROOT_DIR}/../ffmpeg.wz.master/ffdeps_img/ffmpeg/bin/ffprobe"
AUDITOR_PY="${SCRIPT_DIR}/ts_expert_auditor.py"

SRC="${1:-${ROOT_DIR}/../sample/knet_sd_03.ts}"
UDP_PORT="12345"
TEST_DUR="${DUR:-180}"
CAPTURE_TS="${OUT_DIR}/udp_live_stress.ts"
CAPTURE_LOG="${OUT_DIR}/udp_live_stress.log"

export WZ_LICENSE_KEY="${ROOT_DIR}/../wz_license.key"

# 局部清理逻辑：确保后台 ffmpeg 被杀死
trap 'kill $FFM_PID 2>/dev/null; exit 1' SIGINT SIGTERM

echo "=========================================================="
echo "   T-STD UDP STABILITY & TSDUCK AUDIT"
echo "=========================================================="
echo "[*] Cleaning environment..."
fuser -k ${UDP_PORT}/udp 2>/dev/null
rm -f "$CAPTURE_TS" "$CAPTURE_LOG"

echo "[1/3] Launching UDP Pusher (Real-time -re mode)..."
$FFMPEG_BIN -hide_banner -y -v trace -thread_queue_size 128 -rw_timeout 30000000 -fflags +discardcorrupt -re -i "$SRC" \
      -t "$TEST_DUR" \
      -filter_complex "[0:v]fps=fps=25[fg_0_fps];[fg_0_fps]wzaipreopt=enhtype=WZ_FaceMask_MNN:expandRatio=0.1:speedLvlFace=1,wzoptimize=autoenh=0:ynslvl=0:uvnslvl=0:uvenh=0:sharptype=3:yenh=1.6:thrnum=2[fg_0_custom]" \
      -map "[fg_0_custom]" -c:v libwz264 -preset fast \
      -wz264-params "keyint=25:vbv-maxrate=600:vbv-bufsize=600:nal-hrd=cbr:force-cfr=1:aud=1:scenecut=0:b-adapt=0" \
      -c:a aac -b:a 128k \
      -f mpegts -muxrate 1100k -muxdelay 0.9 -pcr_period 30 -pat_period 0.2 -sdt_period 0.25 \
      -mpegts_start_pid 0x21 -mpegts_pcr_pid 0x21 -mpegts_tstd_mode 1 -mpegts_tstd_debug 2 \
      "udp://127.0.0.1:$UDP_PORT?pkt_size=1316" > "$CAPTURE_LOG" 2>&1 &
FFM_PID=$!

echo "[2/3] Capturing bit-accurate stream from UDP port $UDP_PORT..."
# Using nc to capture without stack overhead
timeout $((TEST_DUR + 10)) nc -u -l -p "$UDP_PORT" > "$CAPTURE_TS"

wait $FFM_PID

echo "[3/3] Running Post-Mortem Audits..."
echo "--- T-STD Physical Expert Audit ---"
python3 "$AUDITOR_PY" "$CAPTURE_TS" --vid 0x21 --target 600 --simple --skip 15.0

echo ""
echo "--- TSDuck Industrial Analysis ---"
if command -v tsp &> /dev/null; then
    tsp -I file "$CAPTURE_TS" -P bitrate_monitor -P pcrverify -P continuity -O drop
else
    echo "[WARN] TSDuck (tsp) not found in system. Skipping PCR/Bitrate sub-audit."
fi

echo "=========================================================="
echo "[*] UDP Stability Test finished."
