#!/bin/bash
# 目标：在 feat_for_ts_muxer_tstd (V2) 分支运行，生成故障样本
FFMPEG="/home/lmwang/dev/cae/ffmpeg.wz.master/ffdeps_img/ffmpeg/bin/ffmpeg"
INPUT="/home/lmwang/dev/cae/sample/hd-2026.3.13-10.20~10.25.ts"
OUT_DIR="/home/lmwang/dev/cae/tsanalyzer/output_golden"

mkdir -p "$OUT_DIR"
$FFMPEG -y -hide_banner -i "$INPUT" -t 60 \
    -map 0:v:0 -map 0:a:0 -c:v libwz264 -b:v 1500k -g 25 -c:a mp2 -b:a 128k \
    -f mpegts -muxrate 2300k -muxdelay 0.9 \
    -mpegts_tstd_mode 1 -mpegts_tstd_debug 1 \
    "$OUT_DIR/tstd_1080i_v2_broken.ts" 2>&1 | tee "$OUT_DIR/tstd_1080i_v2_broken.log"
