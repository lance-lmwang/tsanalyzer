#!/bin/bash
# T-STD SRT Transparency Audit (SRS Relay - Bit-Accurate Mode)
# Goal: Prove SRT + SRS transport is 100% bit-transparent without extra null packets.

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/../../.." && pwd)"
OUT_DIR="${ROOT_DIR}/output/srt_srs_audit"
mkdir -p "$OUT_DIR"

FFMPEG_BIN="${ROOT_DIR}/../ffmpeg.wz.master/ffdeps_img/ffmpeg/bin/ffmpeg"
if [ ! -f "$FFMPEG_BIN" ]; then FFMPEG_BIN="ffmpeg"; fi
AUDITOR_PY="${SCRIPT_DIR}/ts_expert_auditor.py"
ALIGN_AUDIT="${SCRIPT_DIR}/tstd_promax_alignment_audit.sh"

SRC_FILE="${1:-SRT_PUSH_AURORA-ZBX_KNET_SD-s6rmgxr_20260312-16.18.04.ts}"
[ -f "$SRC_FILE" ] || SRC="${ROOT_DIR}/../sample/$SRC_FILE"
[ -f "$SRC" ] || SRC="$SRC_FILE"

REF_TS="${OUT_DIR}/reference_tstd.ts"
SRT_TS="${OUT_DIR}/srs_captured.ts"
MUXRATE=1100
SRT_PORT="10080"

echo "=========================================================="
echo "   T-STD SRT SRS TRANSPARENCY (BIT-ACCURATE)"
echo "=========================================================="

# [1/4] Generate Reference
echo "[1/4] Generating Reference TS (Static 1100k CBR)..."
$FFMPEG_BIN -y -hide_banner -i "$SRC" -t 60 \
    -filter_complex "[0:v]fps=fps=25 [fg_fps];[fg_fps]wzaipreopt=enhtype=WZ_FaceMask_MNN:expandRatio=0.1:speedLvlFace=1,wzoptimize=autoenh=0:ynslvl=0:uvnslvl=0:uvenh=0:sharptype=3:yenh=1.6:thrnum=4[fg_out]" \
    -map [fg_out] -c:v:0 libwz264 -g:v:0 25 -preset fast -b:v 600k \
    -wz264-params "keyint=25:vbv-maxrate=600:vbv-bufsize=540:nal-hrd=cbr:force-cfr=1" \
    -c:a copy -f mpegts -muxrate ${MUXRATE}k -pcr_period 35 \
    -mpegts_tstd_mode 1 "$REF_TS" > /dev/null 2>&1

REF_MD5=$(md5sum "$REF_TS" | awk '{print $1}')
PKT_COUNT=$(( $(stat -c%s "$REF_TS") / 188 ))
echo "[*] Baseline: MD5=$REF_MD5, Packets=$PKT_COUNT"

# [2/4] Start SRS
docker rm -f srs-srt-audit 2>/dev/null
docker run --rm -d --name srs-srt-audit -p ${SRT_PORT}:${SRT_PORT}/udp \
    -v "${SCRIPT_DIR}/srs_srt.conf:/usr/local/srs/conf/srt.conf" \
    ossrs/srs:6 ./objs/srs -c conf/srt.conf > /dev/null
sleep 5
SRS_IP=$(docker inspect -f '{{range .NetworkSettings.Networks}}{{.IPAddress}}{{end}}' srs-srt-audit)

# [3/4] Transport (RAW Bitstream via SRT)
echo "[3/4] Transporting via SRS Relay ($SRS_IP)..."

# Capture: RAW SAVE (No regulation, No modifications)
tsp -I srt -c "$SRS_IP:$SRT_PORT" --streamid "#!::r=live/test,m=request" --latency 1000 \
    -P until --packets $PKT_COUNT \
    -O file "$SRT_TS" > "$OUT_DIR/srs_pull.log" 2>&1 &
PULL_PID=$!
sleep 2

# Push: RAW SEND (Realtime pacing only, No extra nulls)
tsp --realtime --final-wait 5000 -I file "$REF_TS" \
    -O srt -c "$SRS_IP:$SRT_PORT" --streamid "#!::r=live/test,m=publish" --latency 1000 > "$OUT_DIR/srs_push.log" 2>&1

# Give puller time to flush its buffer after pusher finishes
sleep 5
kill $PULL_PID 2>/dev/null
docker stop srs-srt-audit > /dev/null 2>&1

# [4/4] Final Verification
if [ -s "$SRT_TS" ]; then
    SRT_MD5=$(md5sum "$SRT_TS" | awk '{print $1}')
    echo ""
    echo "=========================================================="
    echo "          BIT-ACCURACY COMPARISON REPORT"
    echo "=========================================================="
    printf "%-15s | %-20s | %-20s\n" "Metric" "Baseline (Ref)" "SRS Relayed"
    echo "----------------------------------------------------------"
    printf "%-15s | %-20s | %-20s\n" "MD5 Hash" "${REF_MD5:0:16}" "${SRT_MD5:0:16}"
    printf "%-15s | %-20s | %-20s\n" "File Size" "$(stat -c%s "$REF_TS")" "$(stat -c%s "$SRT_TS")"

    echo ""
    echo "--- Physical Metric Consistency ---"
    # Both should have identical physical alignment because they are the same bits
    "$ALIGN_AUDIT" "$REF_TS" $MUXRATE | grep -E "Stability|STATUS" | sed 's/STATUS/REF_STATUS/'
    "$ALIGN_AUDIT" "$SRT_TS" $MUXRATE | grep -E "Stability|STATUS" | sed 's/STATUS/SRT_STATUS/'

    if [ "$REF_MD5" == "$SRT_MD5" ]; then
        echo -e "\n\033[32mVERDICT: SRS RELAY IS 100% BIT-ACCURATE TRANSPARENT.\033[0m"
    else
        echo -e "\n\033[31mVERDICT: BITSTREAM MODIFIED! Check for UDP loss or SRS interference.\033[0m"
        diff <(od -t x1 "$REF_TS" | head -n 100) <(od -t x1 "$SRT_TS" | head -n 100)
    fi
else
    echo "[ERROR] SRT Relay capture failed."
fi
