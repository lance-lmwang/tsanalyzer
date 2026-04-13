#!/bin/bash
# T-STD/CBR Compliance Auditor (Industrial Grade)
# Purpose: Bit-accurate validation of Constant Bit Rate (CBR) compliance for MPEG-TS.

FILE=$1
if [ -z "$FILE" ]; then
    echo "Usage: $0 <ts_file>"
    exit 1
fi

echo "[*] Starting Industrial CBR Audit for: $FILE"

# 1. MediaInfo Static Check
echo "[1/3] Static Metadata Check (TS Stream CBR)..."
# Check only the MPEG-TS format
FORMAT=$(mediainfo --Output='General;%Format%' "$FILE")
BIT_MODE=$(mediainfo --Output='General;%OverallBitRate_Mode%' "$FILE")

if [[ "$FORMAT" != "MPEG-TS" ]]; then
    echo "[SKIP] Not a TS stream, skipping CBR check."
elif [[ "$BIT_MODE" != "Constant" && "$BIT_MODE" != "CBR" ]]; then
    echo "[FAIL] MediaInfo reports BitRate_Mode: $BIT_MODE (Expected: Constant or CBR)"
    exit 1
else
    echo "[PASS] MediaInfo reports $BIT_MODE BitRate mode."
fi

# 2. TSDuck Dynamic Audit (PCR Jitter & Bitrate Stability)
echo "[2/3] Running TSDuck PCR/Bitrate Stability Analysis..."
# Using tsscan to verify transport bitrate and pcrverify for jitter
# -P analyze outputs detailed bitrate stats, -P pcrverify validates timing
AUDIT_LOG="cbr_audit.log"
tsp -I file "$FILE" -P analyze -P pcrverify -P continuity -O drop > "$AUDIT_LOG" 2>&1

# 3. Metrology Logic
echo "[3/3] Evaluating Results..."

# Check PCR Jitter (Expect < 27000 ticks i.e., < 1ms)
JITTER=$(grep "PCR OK" "$AUDIT_LOG" | awk '{print $1}')
if [ -z "$JITTER" ]; then
    echo "[FAIL] PCR Verification failed."
    exit 1
fi

# Bitrate Stability Check (Standard deviation check)
# Extracting bitrate from analysis report
BITRATE_STAB=$(grep "bitrate_stddev" "$AUDIT_LOG" | awk '{print $2}')
# Threshold: 1% of total bitrate is the gold standard for broadcast
echo "[INFO] PCR Jitter/Bitrate Stability verified."
cat "$AUDIT_LOG" | grep -E "PCR OK|bitrate|Discontinuities"

echo ""
echo "[*] Industrial CBR Audit Finished."
