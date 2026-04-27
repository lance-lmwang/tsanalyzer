#!/bin/bash
# T-STD Audio Sanity & Interleaving Audit Tool
# Usage: ./tstd_audio_sanity_audit.sh <ts_file>

TS_FILE=$1
FFPROBE="/home/lmwang/dev/cae/ffmpeg.wz.master/ffdeps_img/ffmpeg/bin/ffprobe"

if [ ! -f "$TS_FILE" ]; then
    echo "Error: File $TS_FILE not found."
    exit 1
fi

echo "=========================================================="
echo "   T-STD PHYSICAL STREAM AUDIT: $(basename $TS_FILE)"
echo "=========================================================="

# 1. Check CC Continuity for Audio PID (0x0101 usually in 1080i)
echo "[*] Auditing Audio CC Continuity..."
AUDIO_PID=$($FFPROBE -v error -select_streams a:0 -show_entries stream=id -of default=noprint_wrappers=1:keyvalue=1 "$TS_FILE" | head -n 1 | cut -d= -f2)
if [ -z "$AUDIO_PID" ]; then AUDIO_PID="0x101"; fi

CC_ERRORS=$($FFPROBE -v error -show_packets -select_streams a:0 "$TS_FILE" 2>&1 | grep -i "corrupt\|mismatch" | wc -l)

if [ "$CC_ERRORS" -gt 0 ]; then
    echo -e "\033[31m[FAIL] Detected $CC_ERRORS physical syntax errors in audio stream!\033[0m"
else
    echo -e "\033[32m[PASS] Physical TS/PES syntax OK.\033[0m"
fi

# 2. Check Startup Skew (A/V Sync at start)
echo "[*] Auditing Startup A/V Skew..."
V_START=$($FFPROBE -v error -select_streams v:0 -show_entries packet=pts_time -of csv=p=0 "$TS_FILE" | head -n 1)
A_START=$($FFPROBE -v error -select_streams a:0 -show_entries packet=pts_time -of csv=p=0 "$TS_FILE" | head -n 1)

SKEW=$(awk "BEGIN {print ($V_START - $A_START) * 1000}")
ABS_SKEW=${SKEW#-}

echo "    - Video Start: ${V_START}s"
echo "    - Audio Start: ${A_START}s"
echo "    - Skew: ${SKEW}ms"

if (( $(echo "$ABS_SKEW > 200" | bc -l) )); then
    echo -e "\033[31m[FAIL] Startup Skew too large (>200ms). Mac preview will likely fail.\033[0m"
else
    echo -e "\033[32m[PASS] Startup alignment OK.\033[0m"
fi

# 3. Check Interleaving Density (Anti-stutter)
echo "[*] Auditing Interleaving Density (First 1MB)..."
# Sample first 1000 packets and count max consecutive packets for any single PID
PACKET_PATTERN=$($FFPROBE -v error -show_packets -of csv=p=0 "$TS_FILE" | head -n 1000 | awk -F, '{print $2}')
MAX_CONSEC_PID=$(echo "$PACKET_PATTERN" | awk '
    $1 == last { count++; if(count>max) max=count }
    $1 != last { count=1; last=$1 }
    END { print max }
')

echo "    - Max consecutive packets from same PID: $MAX_CONSEC_PID"
if [ -z "$MAX_CONSEC_PID" ]; then MAX_CONSEC_PID=0; fi
if [ "$MAX_CONSEC_PID" -gt 400 ]; then
    echo -e "\033[31m[FAIL] Extreme Burst detected! Audio will likely stutter.\033[0m"
else
    echo -e "\033[32m[PASS] Interleaving density looks healthy.\033[0m"
fi

echo "=========================================================="
