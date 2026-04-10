#!/bin/bash
# T-STD Jaco Final Verification
# Purpose: Test 8-hour jump handling using the full original file with -copyts.

ROOT_DIR=$(dirname $(dirname $(dirname $(readlink -f $0))))
OUT_DIR="${ROOT_DIR}/output"
mkdir -p "$OUT_DIR"

FFMPEG_BIN="${ROOT_DIR}/../ffmpeg.wz.master/ffdeps_img/ffmpeg/bin/ffmpeg"
SRC="/home/lmwang/sample/jaco/202508300200_Al-Taawoun_VS_Al-Nassr_2_cut_100M.ts"
MUXRATE=25000000

echo "=== Jaco Jump Test: T-STD with CBR Re-encode ==="

# --- Test 1: T-STD Mode (On) ---
echo "[Test 1] T-STD Muxer (Re-encoding)..."
dst="${OUT_DIR}/jaco_tstd.ts"
log_file="${OUT_DIR}/jaco_tstd.log"

$FFMPEG_BIN -y -v trace -copyts -i "$SRC" \
    -map 0:v:0 -map 0:a:0 \
    -c:a libfdk_aac -b:a 64k \
    -vcodec libwz264 \
    -wz264-params bframes=0:keyint=25:vbv-maxrate=1600:vbv-bufsize=1600:nal-hrd=cbr:force-cfr=1:aud=1 \
    -b:v 1600k -pix_fmt yuv420p \
    -f mpegts -mpegts_flags +pat_pmt_at_frames \
    -muxrate 2000000 -muxdelay 0.9 \
    -mpegts_tstd_mode 1 \
    "$dst" > "$log_file" 2>&1

RET_TSTD=$?
SIZE_TSTD=$(stat -c%s "$dst" 2>/dev/null || echo 0)
echo "[Result] T-STD Exit Code: $RET_TSTD, Output Size: $SIZE_TSTD bytes"

echo "------------------------------------------"
echo "=== Final Analysis ==="
grep "Initializing STC anchor" "$log_file"
grep "STC gap > 1s" "$log_file"
grep "T-STD" "$log_file" | tail -n 10

if [ $SIZE_TSTD -gt 0 ]; then
    echo "[*] Analyzing T-STD Output Compliance..."
    ${ROOT_DIR}/scripts/ffmpeg_tstd/ffmpeg_tstd_verify_compliance.sh "$log_file"
fi
