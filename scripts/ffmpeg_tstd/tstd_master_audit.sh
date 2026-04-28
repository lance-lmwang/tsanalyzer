#!/bin/bash
# T-STD High-Precision Audit Tool (Physical Slot Emulation)

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
OUT_DIR="${ROOT_DIR}/output/promax_matrix"
mkdir -p "$OUT_DIR"

FFMPEG_ROOT="$(cd "$ROOT_DIR/../ffmpeg.wz.master" && pwd)"
ffm="${FFMPEG_ROOT}/ffdeps_img/ffmpeg/bin/ffmpeg"
AUDITOR="${SCRIPT_DIR}/ts_expert_auditor.py"

# Robust Argument Parsing
if [[ -f "$1" ]]; then
    # Mode A: First arg is a specific file
    SRC="$1"
    VBR_TARGET="${2:-800}"
    MUX_TARGET="${3:-1200}"
    DUR="${4:-180}"
    MODE="${5:-1}"
elif [[ "$1" =~ ^[0-9]+$ ]]; then
    # Mode B: First arg is MODE
    MODE="$1"
    VBR_TARGET="${2:-800}"
    MUX_TARGET="${3:-1200}"
    DUR="${4:-180}"
    SRC="${ROOT_DIR}/../sample/SRT_PUSH_AURORA-ZBX_KNET_SD-s6rmgxr_20260312-16.18.04.ts"
else
    # Mode C: No valid args, use defaults
    SRC="${ROOT_DIR}/../sample/SRT_PUSH_AURORA-ZBX_KNET_SD-s6rmgxr_20260312-16.18.04.ts"
    VBR_TARGET="800"
    MUX_TARGET="1200"
    DUR="180"
    MODE="1"
fi

SRC_BASE=$(basename "$SRC" | cut -f 1 -d '.')
ALERTS_TMP="${OUT_DIR}/last_alerts.tmp"

print_header() {
    echo "MODE | V_BIT | MUX |  MEANk |   MAXk |   MINk |  DELTA |  V_DLY  | SCORE"
    echo "---------------------------------------------------------------------------"
}

run_mux() {
    local mode=$1 vbr=$2 mux=$3
    local vbr_val=$(echo "$vbr" | sed 's/[kKmM]//g')
    local mux_val=$(echo "$mux" | sed 's/[kKmM]//g')
    local dst="${OUT_DIR}/${SRC_BASE}_m${mode}_v${vbr_val}.ts"
    local tstd_log="${dst}.log"

    # Production License Activation
    export WZ_LICENSE_KEY="${ROOT_DIR}/../wz_license.key"

    $ffm -hide_banner -y -thread_queue_size 128 -rw_timeout 30000000 -fflags +discardcorrupt \
        -i "$SRC" -t "$DUR" \
        -metadata comment=wzcaetrans \
        -filter_complex "[0:v]fps=fps=25[fg_0_fps];[fg_0_fps]wzaipreopt=enhtype=WZ_FaceMask_MNN:expandRatio=0.1:speedLvlFace=1,wzoptimize=autoenh=0:ynslvl=0:uvnslvl=0:uvenh=0:sharptype=3:yenh=1.6:thrnum=2[fg_0_custom]" \
        -map "[fg_0_custom]" -c:v:0 libwz264 -g:v:0 25 -force_key_frames:v:0 "expr:if(mod(n,25),0,1)" \
        -preset:v:0 fast -wz264-params:v:0 "keyint=25:min-keyint=25:aq-mode=2:aq-weight=0.4:aq-strength=1.0:aq-smooth=1.0:psy-rd=0.3:psy-rd-roi=0.4:qcomp=0.65:rc-lookahead=10:pbratio=1.1:vbv-maxrate=${vbr_val}:vbv-bufsize=${vbr_val}:nal-hrd=cbr:force-cfr=1:aud=1:scenecut=0:b-adapt=0" \
        -map 0:a -c:a:0 copy -map "0:d?" -c:d copy -threads 2 -pix_fmt yuv420p -color_range tv \
        -b:v "${vbr_val}k" -flush_packets 0 -muxrate "${mux_val}k" -inputbw 0 -oheadbw 25 \
        -maxbw 0 -latency 1200000 -muxdelay 1.2 -pcr_period 30 -pat_period 0.2 -sdt_period 0.25 \
        -mpegts_start_pid 0x21 -mpegts_pcr_pid 0x21 -mpegts_tstd_mode "$mode" -mpegts_tstd_debug 2 \
        -max_muxing_queue_size 4096 -max_interleave_delta 30000000 \
        -f mpegts "$dst" > "$tstd_log" 2>&1
}

run_audit() {
    local mode=$1 vbr=$2 mux=$3
    local vbr_val=$(echo "$vbr" | sed 's/[kKmM]//g')
    local mux_val=$(echo "$mux" | sed 's/[kKmM]//g')
    local dst="${OUT_DIR}/${SRC_BASE}_m${mode}_v${vbr_val}.ts"
    local tstd_log="${dst}.log"

    if [ ! -f "$dst" ]; then return; fi

    local audit=$(python3 "$AUDITOR" "$dst" --vid 0x21 --target "$vbr_val" --skip 5 --simple)
    read mean max min std score <<< $audit

    # Calculate Delta (Bitrate Fluctuation)
    local delta=$(echo "$max - $min" | bc)

    local max_delay_val=0
    local max_delay_str="N/A"
    if [ -f "$tstd_log" ]; then
        local max_vbv_pct=$(grep "\[T-STD SEC\]" "$tstd_log" | awk -F'VBV:' '{print $2}' | awk -F'%' '{print $1}' | sort -rn | head -n 1)
        if [ -n "$max_vbv_pct" ]; then
            max_delay_val=$(echo "$max_vbv_pct * 9" | bc)
            max_delay_str="${max_delay_val}ms"
            if [ "$max_delay_val" -gt 2000 ]; then max_delay_str="!!${max_delay_val}"; fi
        fi
    fi

    printf "%4s | %5s | %4s | %6s | %6s | %6s | %6s | %7s | %5s\n" \
           "$mode" "$vbr_val" "$mux_val" "$mean" "$max" "$min" "$delta" "$max_delay_str" "$score"
}

if [ -n "$2" ] || [ -n "$3" ]; then
    SINGLE_RUN=1
    SRC_BASE=$(basename "$SRC" | cut -f 1 -d '.')
    print_header
    run_mux "$MODE" "$VBR_TARGET" "$MUX_TARGET"
    run_audit "$MODE" "$VBR_TARGET" "$MUX_TARGET"
else
    echo "[*] Running High-Precision Matrix Audit (Parallel Mode)..."
    SINGLE_RUN=0
    run_mux 1 600 1100 &
    run_mux 1 800 1200 &
    run_mux 1 1000 1400 &
    run_mux 1 1300 1700 &
    wait

    print_header
    run_audit 1 600 1100
    run_audit 1 800 1200
    run_audit 1 1000 1400
    run_audit 1 1300 1700
fi
