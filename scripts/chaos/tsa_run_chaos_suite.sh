#!/bin/bash
# TSA Chaos Suite - Automated CI/CD Regression

URL=${TSA_METRICS_URL:-"http://localhost:8000/metrics"}
VERIFIER="./scripts/tsa_verify_scenarios.py"
INJECTOR="./scripts/tsa_chaos_injector.sh"

function run_scenario() {
    local scenario=$1
    local params=$2

    echo "========================================"
    echo "Running Scenario: $scenario"
    echo "========================================"

    # 1. Apply chaos
    $INJECTOR apply $scenario $params

    # 2. Verify with polling
    if python3 $VERIFIER --url $URL --scenario $scenario --duration 15; then
        echo "Scenario $scenario: PASSED"
    else
        echo "Scenario $scenario: FAILED"
        $INJECTOR reset
        return 1
    fi

    # 3. Reset
    $INJECTOR reset
    sleep 2
    return 0
}

# Run all scenarios
run_scenario "loss" "5" || exit 1
run_scenario "jitter" "100 50" || exit 1
# Compound impairment: loss delay jitter
run_scenario "compound" "5 100 50" || exit 1

echo "Chaos Suite PASSED"
exit 0
