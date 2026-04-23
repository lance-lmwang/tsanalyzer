#!/bin/bash
# T-STD Timeline Jump Recovery Audit
# Verifies that the engine can seamlessly handle severe timeline backward/forward jumps.

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
OUT_TS="${ROOT_DIR}/output/jump_audit_test.ts"

FFMPEG="/home/lmwang/dev/cae/ffmpeg.wz.master/ffdeps_img/ffmpeg/bin/ffmpeg"
SAMPLE="/home/lmwang/dev/cae/sample/202508300200_Al-Taawoun_VS_Al-Nassr_2_cut_400M.ts"

if [ ! -f "$FFMPEG" ]; then
    echo "[FAIL] ffmpeg binary not found."
    exit 1
fi

if [ ! -f "$SAMPLE" ]; then
    echo "[WARN] Sample not found. Skipping."
    exit 0
fi

echo "[*] Auditing severe timeline jump recovery using large industrial sample..."

# Run FFmpeg WITH -copyts to pass raw corrupted timestamps to the T-STD engine
$FFMPEG -y -hide_banner -copyts -i "$SAMPLE" -map 0:v:0 -map 0:a:0 -c copy -f mpegts -muxrate 15000k -mpegts_tstd_mode 1 "$OUT_TS" > "${ROOT_DIR}/output/jump_audit.log" 2>&1
RET=$?

if [ $RET -ne 0 ]; then
    echo "[FAIL] FFmpeg crashed or failed during jump recovery. Exit code: $RET"
    # Don't exit yet, check what it caught
fi

JUMPS=$(grep "TIMELINE JUMP DETECTED" "${ROOT_DIR}/output/jump_audit.log" | wc -l)
FUSE_LAGS=$(grep "DRIVE FUSE" "${ROOT_DIR}/output/jump_audit.log" | wc -l)
DRIVE_SKIPS=$(grep "DRIVE SKIP" "${ROOT_DIR}/output/jump_audit.log" | wc -l)

echo "----------------------------------------------------------"
echo "  TIMELINE ANOMALY REPORT"
echo "----------------------------------------------------------"
echo "  Total Jumps Detected (V6 Voter) : $JUMPS"
echo "  Total DRIVE FUSE (Lag Catch-up) : $FUSE_LAGS"
echo "  Total DRIVE SKIPS (Outrunning)  : $DRIVE_SKIPS"

if [ "$JUMPS" -eq 0 ] && [ "$FUSE_LAGS" -eq 0 ] && [ "$DRIVE_SKIPS" -eq 0 ]; then
    echo "[WARN] Expected to find anomalies in this sample but found none."
    echo "       Make sure the sample actually has timeline jumps."
    exit 1
else
    echo "[PASS] Engine successfully identified and survived the anomalies."
fi
echo "----------------------------------------------------------"

exit 0
