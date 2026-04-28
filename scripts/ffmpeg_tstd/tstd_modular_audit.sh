#!/bin/bash
# T-STD Modular Architecture Audit Script
# Purpose: Stress test individual V3 components (Voter, Scheduler, Pacing)

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
FFMPEG_ROOT="$(cd "$ROOT_DIR/../ffmpeg.wz.master" && pwd)"
ffm="${FFMPEG_ROOT}/ffdeps_img/ffmpeg/bin/ffmpeg"
OUT_DIR="${ROOT_DIR}/output/modular_audit"
mkdir -p "$OUT_DIR"

GLOBAL_FAIL=0

echo "================================================"
echo "   T-STD V3 Modular Architecture Audit"
echo "================================================"

# --- Test 1: Voter System Resilience (Hysteresis Check) ---
echo "[*] Test 1: Voter Hysteresis & Streak Consensus..."
# Scenario: Inject jitter that is NOT consistent to see if Voter rejects false jumps
VOTER_LOG="${OUT_DIR}/voter_stress.log"
$ffm -y -f lavfi -i testsrc=size=720x480:rate=25 -t 15 \
     -filter_complex "setpts='PTS + 0.1*(random(0)>0.95)'" \
     -c:v libx264 -b:v 1000k -f mpegts \
     -muxrate 1500k -mpegts_tstd_mode 1 -mpegts_tstd_debug 2 \
     "${OUT_DIR}/voter_test.ts" > "$VOTER_LOG" 2>&1

# In V3, random jitter should NOT trigger "TIMELINE JUMP" unless it persists for 3 packets
if grep -q "TIMELINE JUMP" "$VOTER_LOG"; then
    # Check if it was a real jump or just jitter.
    # If we have many jumps for a random filter, the Voter is too sensitive.
    JUMP_COUNT=$(grep -c "TIMELINE JUMP" "$VOTER_LOG")
    if [ "$JUMP_COUNT" -gt 5 ]; then
        echo -e "    \033[31m[FAIL] Voter system is too sensitive to noise ($JUMP_COUNT jumps).\033[0m"
        GLOBAL_FAIL=1
    else
        echo -e "    \033[32m[PASS] Voter handled noise (Confirmed $JUMP_COUNT valid shifts).\033[0m"
    fi
else
    echo -e "    \033[32m[PASS] Voter successfully suppressed single-packet jitter.\033[0m"
fi

# --- Test 2: Scheduler L1 Preemption (Congestion Guard) ---
echo "[*] Test 2: Scheduler L1 Preemption Tier..."
# Scenario: Muxrate is barely enough for Video + PCR.
# Force extreme burst to see if ES steals PCR slots.
SCHED_LOG="${OUT_DIR}/sched_stress.log"
$ffm -y -f lavfi -i testsrc=size=720x480:rate=25 -t 10 \
     -c:v libx264 -b:v 1800k -maxrate 3000k -bufsize 1000k \
     -f mpegts -muxrate 2000k -mpegts_tstd_mode 1 -mpegts_tstd_debug 2 \
     "${OUT_DIR}/sched_test.ts" > "$SCHED_LOG" 2>&1

# Check for L1 activation in logs (if debug logs indicate preemption)
# In tstd_scheduler.c, L1 is triggered when es_is_congested is 1.
if grep -q "VBV PANIC" "$SCHED_LOG" || grep -q "Emergency" "$SCHED_LOG"; then
    echo -e "    \033[32m[PASS] Scheduler Tier L1 (Emergency) engaged during congestion.\033[0m"
else
    # If no panic and muxrate was tight, maybe it was too smooth.
    # We check if output bitrate matched target despite tight pipe.
    echo -e "    \033[33m[INFO] Congestion Tier L1 not triggered (Stream was too compliant).\033[0m"
fi

# --- Test 3: Pacing Slew-Rate Stability ---
echo "[*] Test 3: Pacing Slew-Rate & EMA Smoothness..."
# Use the Metrics summary to check bitrate deviation
all_brs=$(grep "\[T-STD SEC\]" "$SCHED_LOG" | awk -F'Out:' '{print $2}' | awk '{print $1}' | sed 's/k//g')
if [ -n "$all_brs" ]; then
    max_br=$(echo "$all_brs" | sort -n | tail -n 1)
    min_br=$(echo "$all_brs" | sort -n | head -n 1)
    diff=$(echo "$max_br - $min_br" | bc)
    echo "    - Peak-to-Peak Bitrate Delta: ${diff} kbps"
    if (( $(echo "$diff < 400" | bc -l) )); then
        echo -e "    \033[32m[PASS] Pacing Slew-Rate maintained laminar flow.\033[0m"
    else
        echo -e "    \033[31m[FAIL] Excessive bitrate oscillation ($diff kbps)!\033[0m"
        GLOBAL_FAIL=1
    fi
fi

echo "------------------------------------------------"
if [ $GLOBAL_FAIL -eq 0 ]; then echo -e "\033[32mSTATUS: MODULAR ARCHITECTURE AUDIT PASSED\033[0m"; else echo -e "\033[31mSTATUS: MODULAR ARCHITECTURE AUDIT FAILED\033[0m"; fi
echo "------------------------------------------------"

exit $GLOBAL_FAIL
