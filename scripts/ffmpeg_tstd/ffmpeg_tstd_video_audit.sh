#!/bin/bash
# T-STD MG-bitrate Audit Tool (Aligned with ETSI TR 101 290)

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
OUT_DIR="${ROOT_DIR}/output/promax_matrix"
mkdir -p "$OUT_DIR"

FFMPEG_ROOT="$(cd "$ROOT_DIR/../ffmpeg.wz.master" && pwd)"
ffm="${FFMPEG_ROOT}/ffdeps_img/ffmpeg/bin/ffmpeg"
AUDITOR="${SCRIPT_DIR}/ffmpeg_tstd_pcr_sliding_window.py"

show_help() {
    echo "Usage: $0 [input_file] [vbr] [mux] [duration] [mode]"
    exit 0
}

SRC="${1:-/home/lmwang/dev/cae/sample/knet_sd_03.ts}"
VBR_TARGET="${2:-800}"
MUX_TARGET="${3:-1200}"
DUR="${4:-120}"
MODE="${5:-1}"

SRC_BASE=$(basename "$SRC" | cut -f 1 -d '.')

print_header() {
    echo "MODE |   V_BIT |    MUX |     MEANk |      MAXk |      MINk |    ±V_DEVk |   V_JIT% | NO_TOK |   NO_DAT |    I_MEANk |     I_MAXk |     I_MINk |    ±I_DEVk"
    echo "----------------------------------------------------------------------------------------------------------------------------------------------------------------"
}

run_mux() {
    local mode=$1 vbr=$2 mux=$3
    local vbr_val=$(echo "$vbr" | sed 's/[kKmM]//g')
    local mux_val=$(echo "$mux" | sed 's/[kKmM]//g')
    local dst="${OUT_DIR}/${SRC_BASE}_m${mode}_v${vbr_val}.ts"
    $ffm -hide_banner -stream_loop -1 -y -thread_queue_size 128 -rw_timeout 30000000 -fflags +discardcorrupt \
        -i "$SRC" -t "$DUR" \
        -preset fast -wz264-params "keyint=25:vbv-maxrate=${vbr_val}:vbv-bufsize=$((vbr_val/2)):nal-hrd=cbr" \
        -b:v "${vbr_val}k" -muxrate "${mux_val}k" -muxdelay 0.9 -pcr_period 30 \
        -mpegts_start_pid 0x21 -mpegts_tstd_mode "$mode" -mpegts_tstd_debug 2 \
        -f mpegts "$dst" > "${dst}.log" 2>&1
}

run_audit() {
    local mode=$1 vbr=$2 mux=$3
    local vbr_val=$(echo "$vbr" | sed 's/[kKmM]//g')
    local dst="${OUT_DIR}/${SRC_BASE}_m${mode}_v${vbr_val}.ts"
    local tstd_log="${dst}.log"

    if [ ! -f "$dst" ]; then return; fi

    local audit=$(python3 "$AUDITOR" "$dst" --vid_pid 0x21 --muxrate $((mux * 1000)))
    read mean_k max_k min_k dev_k <<< $audit

    local v_jit=$(echo "scale=2; ($dev_k / $mean_k) * 100" | bc -l)

    local no_tok="N/A"; local no_dat="N/A"; local i_mean="N/A"; local i_max="N/A"; local i_min="N/A"; local i_dev="N/A"

    if [ "$mode" -ne 0 ] && grep -q "T-STD METRICS SUMMARY" "$tstd_log"; then
        local pid_line=$(grep "PID 0x0021:" "$tstd_log" | tail -n 1)
        if [ -n "$pid_line" ]; then
            local avg_bps=$(echo "$pid_line" | awk -F'Avg=' '{print $2}' | awk -F',' '{print $1}')
            local max_bps=$(echo "$pid_line" | awk -F'Max=' '{print $2}' | awk -F',' '{print $1}')
            local min_bps=$(echo "$pid_line" | awk -F'Min=' '{print $2}' | awk -F',' '{print $1}')
            local fl_bps=$(echo "$pid_line" | awk -F'Fluct=' '{print $2}' | awk '{print $1}')
            i_mean=$(echo "scale=2; ${avg_bps:-0} / 1000" | bc); i_max=$(echo "scale=2; ${max_bps:-0} / 1000" | bc); i_min=$(echo "scale=2; ${min_bps:-0} / 1000" | bc)
            i_dev=$(printf "±%.2f" "$(echo "scale=2; ${fl_bps:-0} / 2000" | bc)")
        fi
    fi
    printf "%4s | %7s | %6s | %10.2f | %10.2f | %10.2f | %12s | %7s%% | %6s | %8s | %10s | %10s | %10s | %12s\n" \
           "$mode" "$vbr_val" "$mux" "$mean_k" "$max_k" "$min_k" "±$dev_k" "$v_jit" "$no_tok" "$no_dat" "$i_mean" "$i_max" "$i_min" "$i_dev"
}

if [ -n "$2" ] || [ -n "$3" ]; then
    print_header
    run_mux "$MODE" "$VBR_TARGET" "$MUX_TARGET"
    run_audit "$MODE" "$VBR_TARGET" "$MUX_TARGET"
else
    run_mux 1 800 1200 & run_mux 1 1000 1400 & run_mux 1 1300 1700 & wait
    print_header
    run_audit 1 800 1200
    run_audit 1 1000 1400
    run_audit 1 1300 1700
fi
