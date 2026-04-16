#!/bin/bash
FFMPEG_ROOT="/home/lmwang/dev/cae/ffmpeg.wz.master"
ffm="${FFMPEG_ROOT}/ffdeps_img/ffmpeg/bin/ffmpeg"
analyzer="/home/lmwang/dev/cae/tsanalyzer/scripts/ffmpeg_tstd/tstd_bitrate_auditor.py"
SRC="/home/lmwang/dev/cae/sample/af2_srt_src.ts"

run_mode() {
    local mode=$1
    local name=$2
    local log="m${mode}.log"
    echo "[*] Testing $name (Mode $mode)..."
    $ffm -hide_banner -y -i "$SRC" -t 20 \
        -c:v libwz264 -b:v 600k -preset fast \
        -force_key_frames 'expr:eq(mod(n,25),0)' \
        -wz264-params 'keyint=25:vbv-maxrate=600:vbv-bufsize=300:nal-hrd=cbr:force-cfr=1:aud=1' \
        -f mpegts -muxrate 1100k -mpegts_tstd_mode $mode -mpegts_tstd_debug 2 -mpegts_start_pid 0x21 \
        output_m${mode}.ts > "$log" 2>&1

    echo "    - Video PID (0x21) Stats:"
    python3 "$analyzer" --log "$log" --pid 0x21 --window 1.0 --skip 5.0 | grep -E "Mean|Max|Min|Fluctuation" | sed 's/^/      /'
}

run_mode 0 "Native"
run_mode 1 "Strict"
run_mode 2 "Elastic"
