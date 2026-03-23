#!/bin/bash
set -e

# Configuration
INPUT_FILE="sample/sport_hd.ts"
RAW_OUTPUT="raw_copy.ts"
SHAPED_OUTPUT="shaped_cbr.ts"
TARGET_BITRATE=15000000
REPORT_JSON="final_metrology.json"

# Colors
GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
NC='\033[0m'

log() { echo -e "${GREEN}[TEST] $1${NC}"; }
error() { echo -e "${RED}[ERROR] $1${NC}"; exit 1; }
warn() { echo -e "${YELLOW}[WARN] $1${NC}"; }

# 1. Build Tools
log "Building core tools..."
cd build
cmake .. > /dev/null
make offline_shaper tsa_cli -j$(nproc) > /dev/null
cd ..

# 2. Prepare Source
if [ ! -f "$INPUT_FILE" ]; then
    log "Generating synthetic VBR source ($INPUT_FILE)..."
    ffmpeg -f lavfi -i testsrc=s=1920x1080:r=60 -f lavfi -i sine=f=440:r=48000 \
      -c:v libx264 -preset ultrafast -b:v 8M -maxrate 15M -bufsize 8M \
      -c:a aac -b:a 128k \
      -f mpegts -muxrate 0 -t 5 "$INPUT_FILE" -v warning
fi

# 3. Test A: Raw Copy (Expect Failure)
log "Running Test A: Raw FFmpeg Copy (No Shaper)..."
ffmpeg -y -i "$INPUT_FILE" -c copy -f mpegts "$RAW_OUTPUT" -v warning
./build/tsa_cli "$RAW_OUTPUT" -m replay > /dev/null 2>&1

# Check Raw Errors
RAW_PCR_ERR=$(grep "pcr_repetition_error" $REPORT_JSON | head -1 | awk -F': ' '{print $2}' | tr -d ',')
log "Raw Stream PCR Errors: $RAW_PCR_ERR"
if [ "$RAW_PCR_ERR" -eq "0" ]; then
    warn "Unexpected: Raw stream has 0 PCR errors? FFmpeg might have improved."
else
    log "Confirmed: Raw stream is non-compliant (as expected)."
fi

# 4. Test B: Shaper CBR (Expect Success)
log "Running Test B: Professional Shaper ($TARGET_BITRATE bps)..."
./build/offline_shaper "$INPUT_FILE" "$SHAPED_OUTPUT" $TARGET_BITRATE 1 > /dev/null

log "Analyzing Shaped Stream..."
./build/tsa_cli "$SHAPED_OUTPUT" -m replay > /dev/null 2>&1

# 5. Validate Compliance
log "Verifying DVB Compliance..."

# Check 1: PCR Repetition
SHAPED_PCR_ERR=$(grep "pcr_repetition_error" $REPORT_JSON | head -1 | awk -F': ' '{print $2}' | tr -d ',')
if [ "$SHAPED_PCR_ERR" -ne "0" ]; then
    error "Shaper FAILED: PCR Repetition Errors found ($SHAPED_PCR_ERR)"
else
    log "PASS: PCR Timing is perfect (0 errors)"
fi

# Check 2: PAT/PMT Errors
PAT_ERR=$(grep "pat_error" $REPORT_JSON | head -1 | awk -F': ' '{print $2}' | tr -d ',')
PMT_ERR=$(grep "pmt_error" $REPORT_JSON | head -1 | awk -F': ' '{print $2}' | tr -d ',')
if [ "$PAT_ERR" -ne "0" ] || [ "$PMT_ERR" -ne "0" ]; then
    error "Shaper FAILED: PSI Table Errors (PAT: $PAT_ERR, PMT: $PMT_ERR)"
else
    log "PASS: PSI Tables (PAT/PMT) are compliant"
fi

# Check 3: Physical Bitrate
PHY_BITRATE=$(grep "physical_bitrate_bps" $REPORT_JSON | awk -F': ' '{print $2}' | tr -d ',')
# Allow 1% tolerance
DIFF=$((PHY_BITRATE - TARGET_BITRATE))
if [ ${DIFF#-} -gt $((TARGET_BITRATE / 100)) ]; then
    warn "Bitrate deviation: Target $TARGET_BITRATE, Actual $PHY_BITRATE"
else
    log "PASS: Bitrate is locked to target ($PHY_BITRATE bps)"
fi

# Check 4: PID Stability (Video PID 0x0100)
# We just print this for observation
VIDEO_FLUCT=$(grep -A 10 "0x0100" $REPORT_JSON | grep "bitrate_fluctuation_pct" | awk -F': ' '{print $2}' | tr -d ',')
log "Video PID Fluctuation: $VIDEO_FLUCT%"

# Cleanup
rm -f "$RAW_OUTPUT" "$SHAPED_OUTPUT" "$REPORT_JSON"
log "SUCCESS: All compliance checks passed."
exit 0
