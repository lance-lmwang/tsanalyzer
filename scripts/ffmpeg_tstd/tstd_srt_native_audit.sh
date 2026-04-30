#!/bin/bash
# T-STD SRT Native Transparency Audit (PCR Regulated)

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
OUT_DIR="${ROOT_DIR}/output/srt_native_audit"
mkdir -p "$OUT_DIR"

FFMPEG_BIN="${ROOT_DIR}/../ffmpeg.wz.master/ffdeps_img/ffmpeg/bin/ffmpeg"
if [ ! -f "$FFMPEG_BIN" ]; then FFMPEG_BIN="ffmpeg"; fi
AUDITOR_PY="${SCRIPT_DIR}/ts_expert_auditor.py"
ALIGN_AUDIT="${SCRIPT_DIR}/tstd_promax_alignment_audit.sh"

SRC_FILE="${1:-SRT_PUSH_AURORA-ZBX_KNET_SD-s6rmgxr_20260312-16.18.04.ts}"
[ -f "$SRC_FILE" ] || SRC="${ROOT_DIR}/../sample/$SRC_FILE"
[ -f "$SRC" ] || SRC="$SRC_FILE"

REF_TS="${OUT_DIR}/reference_tstd.ts"
SRT_TS="${OUT_DIR}/srt_captured.ts"
MUXRATE=1100
BITRATE_BPS=$((MUXRATE * 1000))
LISTEN_PORT="20080"

echo "=========================================================="
echo "   T-STD NATIVE SRT AUDIT (PCR REGULATED)"
echo "=========================================================="

# [1/3] Generate Reference (sd Template Aligned)
echo "[1/3] Generating Reference TS..."
$FFMPEG_BIN -y -hide_banner -i "$SRC" -t 30 \
    -filter_complex "[0:v]fps=fps=25 [fg_fps];[fg_fps]wzaipreopt=enhtype=WZ_FaceMask_MNN:expandRatio=0.1:speedLvlFace=1,wzoptimize=autoenh=0:ynslvl=0:uvnslvl=0:uvenh=0:sharptype=3:yenh=1.6:thrnum=4[fg_out]" \
    -map [fg_out] -c:v:0 libwz264 -g:v:0 25 -force_key_frames:v:0 'expr:if(mod(n,25),0,1)' -preset:v:0 fast \
    -wz264-params:v:0 "keyint=25:min-keyint=25:aq-mode=2:aq-weight=0.4:aq-strength=1.0:aq-smooth=1.0:psy-rd=0.3:psy-rd-roi=0.4:qcomp=0.65:rc-lookahead=10:pbratio=1.1:vbv-maxrate=600:vbv-bufsize=540:nal-hrd=cbr:force-cfr=1:aud=1:scenecut=0:b-adapt=0" \
    -map 0:a -c:a:0 copy -flags +ilme+ildct \
    -threads 4 -pix_fmt yuv420p -color_range tv -b:v 600k \
    -flush_packets 0 -muxrate ${MUXRATE}k -muxdelay 0.9 -pcr_period 35 \
    -pat_period 0.2 -sdt_period 0.25 -mpegts_start_pid 0x21 -mpegts_tstd_mode 1 \
    -max_muxing_queue_size 4096 -max_interleave_delta 30000000 \
    -f mpegts "$REF_TS" > /dev/null 2>&1

REF_MD5=$(md5sum "$REF_TS" | awk '{print $1}')
PKT_COUNT=$(tsp -I file "$REF_TS" -P count -O drop 2>&1 | grep "packets" | awk '{sum += $4} END {print sum}')

# [2/3] Transport P2P (PCR Regulated)
echo "[2/3] Transporting via Native SRT (P2P regulated)..."
tsp -I srt -l $LISTEN_PORT -P regulate --bitrate $BITRATE_BPS -P until --packets $PKT_COUNT -O file "$SRT_TS" > /dev/null 2>&1 &
RECV_PID=$!
sleep 2

tsp -I file "$REF_TS" -P regulate --bitrate $BITRATE_BPS \
    -O srt -c 127.0.0.1:$LISTEN_PORT --latency 500 > /dev/null 2>&1

sleep 5
kill $RECV_PID 2>/dev/null
SRT_MD5=$(md5sum "$SRT_TS" | awk '{print $1}')

# [3/3] Final Compare
echo ""
echo "=========================================================="
echo "          NATIVE SRT TRANSPARENCY REPORT"
echo "=========================================================="
printf "%-15s | %-20s | %-20s\n" "Metric" "Baseline (Ref)" "SRT Native"
echo "----------------------------------------------------------"
printf "%-15s | %-20s | %-20s\n" "MD5 Hash" "${REF_MD5:0:12}..." "${SRT_MD5:0:12}..."

python3 "$AUDITOR_PY" "$REF_TS" --vid 0x21 --target 600 --simple > /tmp/ref.s
python3 "$AUDITOR_PY" "$SRT_TS" --vid 0x21 --target 600 --simple > /tmp/srt.s
read r_mean r_max r_min r_std r_score <<< $(cat /tmp/ref.s)
read s_mean s_max s_min s_std s_score <<< $(cat /tmp/s.s)

printf "%-15s | %-20s | %-20s\n" "Avg Bitrate" "$r_mean" "$s_mean"
printf "%-15s | %-20s | %-20s\n" "Max Bitrate" "$r_max" "$s_max"
printf "%-15s | %-20s | %-20s\n" "Bitrate StdDev" "$r_std" "$s_std"
echo "----------------------------------------------------------"

echo ""
echo "--- Physical Alignment Check (Promax Mode) ---"
echo "[Baseline Reference]"
"$ALIGN_AUDIT" "$REF_TS" $MUXRATE | grep -E "Stability|STATUS"
echo "[SRT Captured]"
"$ALIGN_AUDIT" "$SRT_TS" $MUXRATE | grep -E "Stability|STATUS"

if [ "$REF_MD5" == "$SRT_MD5" ]; then
    echo -e "\n\033[32mVERDICT: NATIVE SRT (PCR REGULATED) IS BIT-ACCURATE.\033[0m"
else
    echo -e "\n\033[33mVERDICT: MD5 MISMATCH (Check for UDP loss). Comparing physical metrics.\033[0m"
fi
