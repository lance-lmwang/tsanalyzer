#!/bin/bash
# T-STD Comparative Audit: Mode 1 (Strict) vs Mode 2 (Elastic)
# Ensures both modes are validated against the same input.

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
FFMPEG_ROOT="$(cd "$ROOT_DIR/../ffmpeg.wz.master" && pwd)"
ffm="${FFMPEG_ROOT}/ffdeps_img/ffmpeg/bin/ffmpeg"
OUT_DIR="${ROOT_DIR}/output/comparative"
mkdir -p "$OUT_DIR"

src="/home/lmwang/sample/jaco/202508300200_Al-Taawoun_VS_Al-Nassr_2_cut_400M.ts"

# Corrected CBR configuration for ~14Mbps input
v_br="13000k"
v_br_num=13000
muxrate="16000k"

run_mode() {
    local mode=$1
    local name=$2
    local dst="${OUT_DIR}/mode${mode}_${name}.ts"
    local log="${OUT_DIR}/mode${mode}_${name}.log"

    echo "[*] Running Mode $mode ($name) Marathon..."
    $ffm -y -hide_banner -i "$src" -frames:v 20000 \
         -c:v libwz264 -b:v $v_br -preset ultrafast \
         -wz264-params "keyint=25:vbv-maxrate=$v_br_num:vbv-bufsize=$v_br_num:nal-hrd=cbr:force-cfr=1" \
         -c:a copy \
         -f mpegts -muxrate $muxrate -mpegts_tstd_mode $mode -tstd_params "debug=2" \
         "$dst" > "$log" 2>&1

    if [ $? -eq 0 ]; then
        echo -e "    \033[32m[PASS] Mode $mode finished successfully.\033[0m"
    else
        echo -e "    \033[31m[FAIL] Mode $mode crashed!\033[0m"
    fi
}

echo "=== Comparative T-STD Test (Mode 1 vs Mode 2) ==="
run_mode 1 "Strict"
run_mode 2 "Elastic"
echo "=== Comparison Complete ==="
EOF
