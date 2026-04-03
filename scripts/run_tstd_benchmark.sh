#!/bin/bash
# T-STD Industrial Metrology Benchmark Script v1.3
# Goal: Drive FFmpeg to generate T-STD telemetry and then audit compliance.

set -e

SCRIPT_DIR=$(cd $(dirname ${BASH_SOURCE[0]}) && pwd)
TSA_DIR=$(cd $SCRIPT_DIR/.. && pwd)
FF_SRC=$(cd $TSA_DIR/../ffmpeg.wz.master 2>/dev/null && pwd || echo "")
AUDITOR="$SCRIPT_DIR/verify_tstd_compliance.sh"
TRACE_LOG="tstd_telemetry.log"
FFMPEG_BIN="$FF_SRC/ffdeps_img/ffmpeg/bin/ffmpeg"

DO_BUILD=true
if [[ "$1" == "--no-build" ]]; then
    DO_BUILD=false
    echo "[*] Skip compilation mode enabled."
fi

# 1. Environment Validation
echo "==== 0. Environment Validation ===="
if [ -z "$FF_SRC" ] || [ ! -d "$FF_SRC" ]; then
    echo "[!] Error: FFmpeg source directory not found at ../ffmpeg.wz.master"
    echo "    Expected structure: parent_dir/{tsanalyzer, ffmpeg.wz.master}"
    exit 1
fi

INPUT_FILE="$TSA_DIR/../sample/input.mp4"
if [ ! -f "$INPUT_FILE" ]; then
    echo "[!] Error: Sample input file not found at $INPUT_FILE"
    exit 1
fi

if [ ! -f "$AUDITOR" ]; then
    echo "[!] Error: Compliance auditor script not found at $AUDITOR"
    exit 1
fi

# 2. Compilation Phase
if [ "$DO_BUILD" = true ]; then
    echo "==== 1. Compile Latest Telemetry Logic ===="
    pushd "$FF_SRC" > /dev/null
    if [ ! -f "./scripts_ci/docker_build.sh" ]; then
        echo "[!] Error: Cannot find ./scripts_ci/docker_build.sh in $FF_SRC"
        exit 1
    fi
    ./scripts_ci/docker_build.sh
    popd > /dev/null
    echo "Compilation successful."
else
    echo "==== 1. Skip Compilation ===="
fi

# 3. Check Binary Existence
if [ ! -f "$FFMPEG_BIN" ]; then
    echo "[!] Error: FFmpeg binary not found at $FFMPEG_BIN"
    echo "    Did you run the build once?"
    exit 1
fi

# 4. Data Collection Phase
echo "==== 2. Collect Telemetry Data (CBR 1.6Mbps Video, 2Mbps Mux, 30s) ===="
echo "[*] Running FFmpeg with T-STD mode 1 in debug mode..."
timeout -k 5 60 "$FFMPEG_BIN" -v debug -y -nostdin -stream_loop -1 -i "$INPUT_FILE" -t 30 \
    -c:v libx264 -b:v 1600k -maxrate 1600k -bufsize 1600k -nal-hrd cbr \
    -muxrate 2000000 -max_delay 900000 -mpegts_tstd_mode 1 test_bench.ts > "$TRACE_LOG" 2>&1 || true

LOG_LINES=$(wc -l < "$TRACE_LOG")
echo "Data collection complete: $LOG_LINES lines of logs."

# 5. Handover to Compliance Auditor
echo "==== 3. Handover to Compliance Auditor ===="
"$AUDITOR" "$TRACE_LOG"
