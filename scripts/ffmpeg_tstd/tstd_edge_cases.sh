#!/bin/bash
# T-STD Edge Case Resilience Suite (Final Production Stability Edition)

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT_DIR="${ROOT_DIR}/output/edge_cases"
mkdir -p "$OUT_DIR"

FFMPEG="/home/lmwang/dev/cae/ffmpeg.wz.master/ffdeps_img/ffmpeg/bin/ffmpeg"
SRC="/home/lmwang/dev/cae/sample/SRT_PUSH_AURORA-ZBX_KNET_SD-s6rmgxr_20260312-16.18.04.ts"

GLOBAL_SUCCESS=1

check_result() {
    if [ $? -eq 0 ]; then echo -e "\033[32m[PASS]\033[0m $1"; else echo -e "\033[31m[FAIL]\033[0m $1"; GLOBAL_SUCCESS=0; fi
}

echo "=========================================================="
echo "   T-STD EDGE CASE AUDIT (Full Sync)"
echo "=========================================================="

rm -f "$OUT_DIR"/*.log "$OUT_DIR"/*.ts

OPTS=(
    -y -loglevel info -hide_banner -thread_queue_size 128 -rw_timeout 30000000 -fflags +discardcorrupt
    -i "$SRC" -metadata comment=wzcaetrans
    -filter_complex "[0:v]fps=fps=50[fg_0_fps];[fg_0_fps]wzaipreopt=enhtype=WZ_FaceMask_MNN:expandRatio=0.1:speedLvlFace=1,wzoptimize=autoenh=0:ynslvl=0:uvnslvl=0:uvenh=0:sharptype=3:yenh=1.6:thrnum=2[fg_0_custom]"
    -map "[fg_0_custom]" -c:v:0 libwz264 -g:v:0 50 -force_key_frames:v:0 "expr:if(mod(n,50),0,1)" -preset fast
    -wz264-params "keyint=50:min-keyint=50:aq-mode=2:aq-weight=0.4:aq-strength=1.0:aq-smooth=1.0:psy-rd=0.3:psy-rd-roi=0.4:qcomp=0.65:rc-lookahead=10:pbratio=1.1:vbv-maxrate=600:vbv-bufsize=600:nal-hrd=cbr:force-cfr=1:aud=1:scenecut=0:b-adapt=0"
    -map 0:a -threads 2 -pix_fmt yuv420p -color_range tv -b:v 600k -flush_packets 0 -muxrate 1200k
    -latency 1000000 -muxdelay 0.9 -pcr_period 30 -pat_period 0.2 -sdt_period 0.25
    -mpegts_start_pid 0x21 -mpegts_tstd_mode 1 -mpegts_tstd_debug 1 -max_muxing_queue_size 4096 -max_interleave_delta 0
)

echo "[1/3] Startup: Timeline Anchoring..."
"$FFMPEG" "${OPTS[@]}" -t 10 -c:a copy "$OUT_DIR/startup.ts" > "$OUT_DIR/startup.log" 2>&1
grep -q "First Packet" "$OUT_DIR/startup.log"
check_result "Startup anchored."

echo "[2/3] Burst: AU Queue Stress..."
"$FFMPEG" "${OPTS[@]}" -t 15 -c:a copy "$OUT_DIR/burst.ts" > "$OUT_DIR/burst.log" 2>&1
grep -i -q "AU FIFO overflow" "$OUT_DIR/burst.log"
if [ $? -ne 0 ]; then echo -e "\033[32m[PASS]\033[0m Queue OK."; else echo -e "\033[31m[FAIL]\033[0m Queue overflow!"; GLOBAL_SUCCESS=0; fi

echo "[3/3] Drain: Alignment (1.5s Tolerant)..."
"$FFMPEG" "${OPTS[@]}" -t 15 -c:a mp2 -b:a 128k "$OUT_DIR/drain.ts" > "$OUT_DIR/drain.log" 2>&1
V_DUR=$(mediainfo --Inform="Video;%Duration%" "$OUT_DIR/drain.ts")
A_DUR=$(mediainfo --Inform="Audio;%Duration%" "$OUT_DIR/drain.ts")
# 1920ms 是源文件固有的 Offset，只要 (A - V) 不超过 2200ms，说明 Drain 阶段正常收尾且对齐
DIFF=$(( A_DUR - V_DUR ))
if [ $DIFF -lt 2200 ] && [ $DIFF -gt 1500 ]; then
    echo -e "\033[32m[PASS]\033[0m Aligned: V=${V_DUR}ms, A=${A_DUR}ms (Offset: ${DIFF}ms)."
else
    echo -e "\033[31m[FAIL]\033[0m Alignment fail: Offset ${DIFF}ms."
    GLOBAL_SUCCESS=0
fi

echo "=========================================================="
[ $GLOBAL_SUCCESS -eq 1 ] || exit 1
