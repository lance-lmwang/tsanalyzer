#!/bin/bash
# End-to-End Validation for libtsshaper in FFmpeg

# Use the FFmpeg in the workspace
FFMPEG="../../ffmpeg.wz-master-n7.1/ffmpeg"
TSA_SLICER="./bin/tsa_slicer"
OUTPUT="e2e_tstd_test.ts"
BITRATE="8000000" # 8 Mbps
MUXRATE="10000000" # 10 Mbps

echo "=== TSA: Starting End-to-End CBR/T-STD Validation ==="

if [ ! -f "$FFMPEG" ]; then
    echo "!!! ERROR: FFmpeg not found at $FFMPEG. Please build FFmpeg first. !!!"
    exit 1
fi

# Run FFmpeg with libx264 CBR and TSA Shaper
echo "--- Running FFmpeg with TSA Shaper at $MUXRATE bps ---"
$FFMPEG -hide_banner -y -re -f lavfi -i testsrc=size=1920x1080:rate=25 \
    -c:v libx264 -preset ultrafast \
    -b:v $BITRATE -maxrate $BITRATE -minrate $BITRATE -bufsize $BITRATE \
    -nal-hrd cbr \
    -f mpegts -tsa_shaper 1 -tsa_bitrate $MUXRATE \
    -t 10 "$OUTPUT"

if [ $? -eq 0 ]; then
    echo "--- Build SUCCESS: Output file generated: $OUTPUT ---"
    ls -lh "$OUTPUT"

    if [ -f "$TSA_SLICER" ]; then
        echo "--- Bitrate & PCR Analysis (via TSA Slicer) ---"
        $TSA_SLICER "$OUTPUT" | head -n 20
    else
        echo "--- Warning: TSA Slicer not found, skipping detailed analysis. ---"
    fi
else
    echo "!!! ERROR: FFmpeg command failed !!!"
    exit 1
fi
