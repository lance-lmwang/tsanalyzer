#!/bin/bash
# T-STD Master Validation Harness
# Executing the full spectrum of T-STD validation tools to ensure production readiness.

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
LOG_DIR="${ROOT_DIR}/output/final_verify"
mkdir -p "$LOG_DIR"

export WZ_LICENSE_KEY="/home/lmwang/dev/cae/wz_license.key"

# 确保在脚本退出时恢复终端状态 (解决打字没有回显的问题)
trap "stty echo" EXIT

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
run_stage "CI_REGRESSION" "$SCRIPT_DIR/tstd_regression_suite.sh $@"

# --- STAGE 2: Physical Matrix Audit (600k-1300k, Long-run) ---
# Using DUR=60 for a quick full-spectrum verify; use DUR=600 for deep stress
export DUR=60
run_stage "MATRIX_AUDIT" "$SCRIPT_DIR/tstd_master_audit.sh 1"

# --- STAGE 3: Physical Truth Alignment ---
run_stage "TRUTH_CHECK" "$SCRIPT_DIR/tstd_physical_audit.sh"

# --- STAGE 4: Real-time UDP Stress & TSDuck Audit ---
# Note: Performs real-time -re pushing to loopback and captures for PCR analysis
export DUR=60
run_stage "UDP_STABILITY" "$SCRIPT_DIR/tstd_udp_stability.sh"

# --- STAGE 4.5: PSI Interval Compliance Audit ---
run_stage "PSI_COMPLIANCE" "$SCRIPT_DIR/tstd_psi_audit.sh"

# --- STAGE 4.6: Deep Timeline Jump Audit ---
run_stage "JUMP_RECOVERY" "$SCRIPT_DIR/tstd_jump_audit.sh"

# --- STAGE 4.7: A/V Boundary & Continuity Audit ---
run_stage "BOUNDARY_AUDIT" "python3 $SCRIPT_DIR/tstd_audit_v2.py $ROOT_DIR/output/jump_audit_test.ts"

# --- STAGE 4.8: Audio-Only Resilience Audit ---
run_stage "AUDIO_ONLY" "$SCRIPT_DIR/tstd_audio_only_audit.sh"

# --- STAGE 4.9: Chaos Jitter Resilience Audit ---
run_stage "CHAOS_AUDIT" "$SCRIPT_DIR/tstd_chaos_audit.sh"

# --- STAGE 5: Edge Case Resilience Audit (Startup/Burst/Drain) ---
run_stage "EDGE_CASES" "$SCRIPT_DIR/tstd_edge_cases.sh"

# --- STAGE 6: Legacy vs T-STD Comparative Advantage Audit ---
run_stage "LEGACY_COMPARE" "$SCRIPT_DIR/tstd_legacy_compare_audit.sh"

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

# --- Stage 18: Shapability Matrix & 84k Delta Audit (Hard Requirement) ---
echo ""
echo ">>> STAGE: SHAPABILITY_MATRIX_AUDIT (Limit: 84k)"
echo "----------------------------------------------------------"
MATRIX_SCRIPT="${SCRIPT_DIR}/tstd_shapability_matrix.sh"
if [ -f "$MATRIX_SCRIPT" ]; then
    echo "[*] Running industrial shapability matrix..."
    $MATRIX_SCRIPT all > "${LOG_DIR}/shapability_audit.log" 2>&1

    # 提取所有 Bitrate Stats 行中的 Delta 值进行判定
    # 格式示例: Bitrate Stats : Min: 638k, Max: 686k, Delta: 48k, ...
    DELTAS=$(grep "Bitrate Stats" "${LOG_DIR}/shapability_audit.log" | awk -F'Delta: ' '{print $2}' | awk -F'k' '{print $1}')

    FAIL_COUNT=0
    for d in $DELTAS; do
        if [ "$d" -ge 84 ]; then
            echo -e "\033[31m[FAIL] Bitrate Delta ${d}k exceeds hard limit (84k)!\033[0m"
            FAIL_COUNT=$((FAIL_COUNT + 1))
        fi
    done

    if [ $FAIL_COUNT -eq 0 ]; then
        echo -e "\033[32m[PASS] All matrix templates meet the 84k smoothing requirement.\033[0m"
        grep "Bitrate Stats" "${LOG_DIR}/shapability_audit.log" | sed 's/^/    - /'
    else
        echo -e "\033[31m[FAIL] Shapability Matrix Audit failed with $FAIL_COUNT violations.\033[0m"
        GLOBAL_FAIL=1
    fi
else
    echo "[WARN] tstd_shapability_matrix.sh not found, skipping Stage 18."
fi
