#!/bin/bash
# T-STD Bitrate Stability: Pixel-Perfect Alignment Test
# Purpose: Use the EXACT same command as smoke_test.sh to find the 148k trigger.

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
OUT_DIR="${ROOT_DIR}/output"
mkdir -p "$OUT_DIR"

FFMPEG_ROOT="$(cd "$ROOT_DIR/../ffmpeg.wz.master" && pwd)"
ffm="${FFMPEG_ROOT}/ffdeps_img/ffmpeg/bin/ffmpeg"

# 使用与 smoke_test 完全一致的源和参数
src="/home/lmwang/dev/cae/sample/knet_sd_03.ts"
bitrate="1600k"
muxrate=2000000
log_file="${OUT_DIR}/align_final.log"
dst="${OUT_DIR}/align_final.ts"

echo "[*] Running Pixel-Perfect Alignment Test..."

# 核心：这行命令必须与 smoke_test.sh 的 PHASE 1 完全一致
$ffm -y -v trace -i "$src" \
      -t 15 \
      -map 0:v:0 -map 0:a:0 \
      -c:a libfdk_aac -profile:a aac_low -b:a 128k \
      -vcodec libwz264 \
      -wz264-params bframes=0:keyint=25:vbv-maxrate=$bitrate:vbv-bufsize=$bitrate:nal-hrd=cbr:force-cfr=1:aud=1 -threads 4 \
      -b:v $bitrate -profile:v Main -preset medium -pix_fmt yuv420p \
      -force_key_frames 'expr:gte(t,n_forced*1)' \
      -dn \
      -flush_packets 0 \
      -metadata service_name=wz_tstd \
      -metadata service_provider=wz \
      -f mpegts -mpegts_flags +pat_pmt_at_frames \
      -muxrate $muxrate -muxdelay 0.9 \
      -pcr_period 30 -pat_period 0.1 -sdt_period 0.25 \
      -mpegts_tstd_mode 1 \
      "$dst" > "$log_file" 2>&1

echo "[*] Analyzing results..."
python3 scripts/ffmpeg_tstd/tstd_bitrate_auditor.py --log "$log_file" --pid 0x0100 --window 1.0 --skip 5.0
