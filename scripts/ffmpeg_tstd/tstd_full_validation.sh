#!/bin/bash
# T-STD Master Validation Harness
# Executing the full spectrum of T-STD validation tools to ensure production readiness.

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
LOG_DIR="${ROOT_DIR}/output/final_verify"
mkdir -p "$LOG_DIR"

export WZ_LICENSE_KEY="/home/lmwang/dev/cae/wz_license.key"

echo "=========================================================="
echo "   T-STD FULL VALIDATION HARNESS (Industrial Grade)"
echo "=========================================================="
echo "Started at: $(date)"
echo ""

GLOBAL_SUCCESS=1

run_stage() {
    local name=$1
    local cmd=$2
    echo ">>> STAGE: $name"
    echo "----------------------------------------------------------"
    eval "$cmd"
    local ret=$?
    if [ $ret -eq 0 ]; then
        echo -e "\n\033[32m[PASS] STAGE: $name successful.\033[0m"
    else
        echo -e "\n\033[31m[FAIL] STAGE: $name failed with exit code $ret.\033[0m"
        GLOBAL_SUCCESS=0
    fi
    echo "----------------------------------------------------------"
    echo ""
}

# --- STAGE 1: Automated Regression Suite (Phases 1-13) ---
run_stage "CI_REGRESSION" "./scripts/ffmpeg_tstd/tstd_regression_suite.sh $@"

# --- STAGE 2: Physical Matrix Audit (600k-1300k, Long-run) ---
# Using DUR=60 for a quick full-spectrum verify; use DUR=600 for deep stress
export DUR=60
run_stage "MATRIX_AUDIT" "./scripts/ffmpeg_tstd/tstd_master_audit.sh 1"

# --- STAGE 3: Physical Truth Alignment ---
run_stage "TRUTH_CHECK" "./scripts/ffmpeg_tstd/tstd_physical_audit.sh"

# --- STAGE 4: Real-time UDP Stress & TSDuck Audit ---
# Note: Performs real-time -re pushing to loopback and captures for PCR analysis
export DUR=60
run_stage "UDP_STABILITY" "./scripts/ffmpeg_tstd/tstd_udp_stability.sh"

# --- STAGE 5: Edge Case Resilience Audit (Startup/Burst/Drain) ---
run_stage "EDGE_CASES" "./scripts/ffmpeg_tstd/tstd_edge_cases.sh"

# --- Final Summary ---
echo "=========================================================="
if [ $GLOBAL_SUCCESS -eq 1 ]; then
    echo -e "\033[32mOVERALL STATUS: FINAL VALIDATION PASSED (GOLDEN)\033[0m"
else
    echo -e "\033[31mOVERALL STATUS: VALIDATION FAILED. REVIEW LOGS.\033[0m"
fi
echo "Finished at: $(date)"
echo "=========================================================="

[ $GLOBAL_SUCCESS -eq 1 ] || exit 1
