#!/bin/bash

# 配置路径
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/../../.." && pwd)"
FFMPEG_ROOT="$(cd "$ROOT_DIR/../ffmpeg.wz.master" && pwd)"

FFMPEG="${FFMPEG_ROOT}/ffdeps_img/ffmpeg/bin/ffmpeg"
FFPROBE="${FFMPEG_ROOT}/ffdeps_img/ffmpeg/bin/ffprobe"
SAMPLE_DIR="${ROOT_DIR}/../sample"
OUTPUT_DIR="${ROOT_DIR}/output"
mkdir -p "$OUTPUT_DIR"

# --- 唯一物理真值引擎 (封装所有参数，仅 Debug 可变) ---
exec_tstd_transcode() {
    local INPUT=$1; local BV=$2; local MUX=$3; local PCR=$4; local MUXD=$5;
    local VBV_K=$6; local DEBUG=$7; local OUT_TS=$8; local LOG=$9; shift 9;
    local EXTRA="$@"

    $FFMPEG -y -nostdin -hide_banner -thread_queue_size 128 -rw_timeout 30000000 -fflags +discardcorrupt \
        -i "$INPUT" -t 30 \
        -filter_complex "[0:v]fps=fps=25 [fg_fps];[fg_fps]wzaipreopt=enhtype=WZ_FaceMask_MNN:expandRatio=0.1:speedLvlFace=1,wzoptimize=autoenh=0:ynslvl=0:uvnslvl=0:uvenh=0:sharptype=3:yenh=1.6:thrnum=4[fg_out]" \
        -map [fg_out] -c:v:0 libwz264 -g:v:0 25 -force_key_frames:v:0 'expr:if(mod(n,25),0,1)' -preset:v:0 fast \
        -wz264-params:v:0 "keyint=25:min-keyint=25:aq-mode=2:aq-weight=0.4:aq-strength=1.0:aq-smooth=1.0:psy-rd=0.3:psy-rd-roi=0.4:qcomp=0.65:rc-lookahead=10:pbratio=1.1:vbv-maxrate=${BV%k}:vbv-bufsize=${VBV_K}:nal-hrd=cbr:force-cfr=1:aud=1:scenecut=0:b-adapt=0" \
        -map 0:a -c:a:0 copy $EXTRA \
        -threads 4 -pix_fmt yuv420p -color_range tv -b:v $BV \
        -flush_packets 0 -muxrate $MUX -muxdelay $MUXD -pcr_period $PCR \
        -pat_period 0.2 -sdt_period 0.25 -mpegts_start_pid 0x21 -mpegts_tstd_mode 1 \
        -mpegts_tstd_debug $DEBUG \
        -max_muxing_queue_size 4096 -max_interleave_delta 30000000 \
        -f mpegts "$OUT_TS" > "$LOG" 2>&1
    sync
}

# 调度与分析函数
run_test() {
    NAME=$1; FILE=$2; BV=$3; MUX=$4; PCR=$5; VBV_OVERRIDE=$6

    if [[ "$VBV_OVERRIDE" =~ ^[0-9]+$ ]]; then
        VBV_BUF=$VBV_OVERRIDE
        MUXDELAY=$(awk "BEGIN {printf \"%.2f\", $VBV_BUF / ${BV%k}}")
        shift 6
    else
        MUXDELAY=0.9
        VBV_BUF=$(awk "BEGIN {printf \"%d\", ${BV%k} * $MUXDELAY}")
        shift 5
    fi
    EXTRA_ARGS="$@"

    INPUT_PATH="$SAMPLE_DIR/$FILE"
    [ ! -f "$INPUT_PATH" ] && return

    LOG_FILE="$OUTPUT_DIR/tstd_${NAME}.log"
    OUT_TS="$OUTPUT_DIR/tstd_${NAME}.ts"

    echo "=========================================================="
    echo " RUNNING CASE: $NAME ($BV, Target Debug: ${TSTD_DEBUG:-1})"
    echo "=========================================================="

    exec_tstd_transcode "$INPUT_PATH" "$BV" "$MUX" "$PCR" "$MUXDELAY" "$VBV_BUF" "${TSTD_DEBUG:-1}" "$OUT_TS" "$LOG_FILE" $EXTRA_ARGS

    if [ "${TSTD_DEBUG}" == "0" ]; then return; fi

    # 物理分析
    python3 "$SCRIPT_DIR/ts_shapability_analyzer.py" "$LOG_FILE" "$MUX" "$MUXDELAY"
    "$SCRIPT_DIR/ts_clock_closure_audit.sh" "$OUT_TS"
    "$SCRIPT_DIR/tstd_promax_alignment_audit.sh" "$OUT_TS" "${MUX%k}"
}

verify_debug_impact() {
    echo "=========================================================="
    echo "   FULL MATRIX CONSISTENCY AUDIT: DEBUG ON vs DEBUG OFF  "
    echo "=========================================================="

    # Define templates: name file bv mux pcr vbv_override
    TEMPLATES=(
        "sd SRT_PUSH_AURORA-ZBX_KNET_SD-s6rmgxr_20260312-16.18.04.ts 600k 1100k 35 1.0"
        "720p HD720p_4Mbps.ts 1300k 1700k 35 1.0"
        "1080i hd-2026.3.13-10.20~10.25.ts 1500k 2300k 35 1.0"
        "1080p_high hd-2026.3.13-10.20~10.25.ts 4000k 4800k 35 2400"
    )

    for entry in "${TEMPLATES[@]}"; do
        read -r t_name t_file t_bv t_mux t_pcr t_vbv <<< "$entry"

        echo "[*] Auditing Template: $t_name ($t_bv)..."

        # 1. Debug ON
        exec_tstd_transcode "$SAMPLE_DIR/$t_file" "$t_bv" "$t_mux" "$t_pcr" 0.9 "$t_vbv" 1 "$OUTPUT_DIR/cmp_${t_name}_on.ts" "/tmp/log_on.txt"
        d_on=$("$SCRIPT_DIR/tstd_promax_alignment_audit.sh" "$OUTPUT_DIR/cmp_${t_name}_on.ts" "${t_bv%k}" | grep "Delta:" | awk '{print $7}')

        # 2. Debug OFF
        exec_tstd_transcode "$SAMPLE_DIR/$t_file" "$t_bv" "$t_mux" "$t_pcr" 0.9 "$t_vbv" 0 "$OUTPUT_DIR/cmp_${t_name}_off.ts" "/tmp/log_off.txt"
        d_off=$("$SCRIPT_DIR/tstd_promax_alignment_audit.sh" "$OUTPUT_DIR/cmp_${t_name}_off.ts" "${t_bv%k}" | grep "Delta:" | awk '{print $7}')

        printf "    - Result for %-10s: ON=%-5s | OFF=%-5s | " "$t_name" "$d_on" "$d_off"
        if [ "$d_on" == "$d_off" ]; then
            echo -e "\033[32m[PASS]\033[0m"
        else
            # Calculate numerical diff
            n_on=$(echo "$d_on" | sed 's/k,//')
            n_off=$(echo "$d_off" | sed 's/k,//')
            n_diff=$(echo "$n_on - n_off" | bc | sed 's/-//')
            if [ "$n_diff" -le 3 ]; then
                echo -e "\033[32m[PASS] (Minor $n_diff k drift)\033[0m"
            else
                echo -e "\033[31m[FAIL] Significant Variance ($n_diff k)!\033[0m"
                GLOBAL_FAIL=1
            fi
        fi
        rm -f "$OUTPUT_DIR/cmp_${t_name}_on.ts" "$OUTPUT_DIR/cmp_${t_name}_off.ts"
    done

    [ "$GLOBAL_FAIL" == "1" ] && exit 1
    echo "=========================================================="
    echo -e "\033[32mFINAL STATUS: DEBUG IMPACT AUDIT PASSED FOR ALL TEMPLATES\033[0m"
}

case "$1" in
    sd)    run_test "sd" "SRT_PUSH_AURORA-ZBX_KNET_SD-s6rmgxr_20260312-16.18.04.ts" "600k" "1100k" 35 -flags +ilme+ildct ;;
    720p)  run_test "720p" "HD720p_4Mbps.ts" "1300k" "1700k" 35 ;;
    1080i) run_test "1080i" "hd-2026.3.13-10.20~10.25.ts" "1500k" "2300k" 35 -flags +ilme+ildct ;;
    1080p_high) run_test "1080p_high" "hd-2026.3.13-10.20~10.25.ts" "4000k" "4800k" 35 2400 ;;
    compare) verify_debug_impact ;;
    all) $0 sd; $0 720p; $0 1080i; $0 compare ;;
    *) echo "Usage: $0 {sd|720p|1080i|1080p_high|compare|all}"; exit 1 ;;
esac
