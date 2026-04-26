#!/bin/bash
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT_DIR="${ROOT_DIR}/output"
mkdir -p "$OUT_DIR"
FFMPEG="/home/lmwang/dev/cae/ffmpeg.wz.master/ffdeps_img/ffmpeg/bin/ffmpeg"
SRC="${ROOT_DIR}/sample/knet_sd_03.ts"
[ ! -f "$SRC" ] && SRC="${ROOT_DIR}/sample/af2_srt_src.ts"
DST="${OUT_DIR}/psi_stress.ts"
LOG="${OUT_DIR}/psi_stress.log"

echo "[*] Launching PSI Stress Test (High Pressure Mode)..."
$FFMPEG -y -i "$SRC" -t 30 \
    -c:v libwz264 -b:v 1500k -preset ultrafast \
    -f mpegts -muxrate 1600000 \
    -mpegts_flags +pat_pmt_at_frames \
    -pcr_period 20 -pat_period 0.1 -sdt_period 0.2 \
    -mpegts_tstd_mode 1 -tstd_params "debug=2" \
    "$DST" > "$LOG" 2>&1

echo "[*] Compliance Audit..."
python3 scripts/ffmpeg_tstd/tstd_telemetry_analyzer.py "$LOG"
