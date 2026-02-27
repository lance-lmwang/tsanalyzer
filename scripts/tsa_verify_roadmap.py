#!/usr/bin/env python3
import subprocess
import json
import time
import os

# TsAnalyzer Pro Roadmap Validation Suite

BIN = "./build/tsb"
RESULTS_FILE = "roadmap_validation.json"

def run_accuracy_test():
    print("Testing RST Accuracy (Target: <= +/-1s error)...")
    # We use tsb to simulate a scenario and check RST
    # Since tsb is a benchmark, we'll use a specialized test for this.
    res = subprocess.run(["./build/test_tstd_underflow"], capture_output=True, text=True)
    if res.returncode == 0:
        print("RST Accuracy: PASSED")
        return True
    else:
        print("RST Accuracy: FAILED")
        return False

def run_rca_precision_test():
    print("Testing RCA Precision (Target: >= 98%)...")
    res = subprocess.run(["./build/test_rca_scoring_v2"], capture_output=True, text=True)
    if res.returncode == 0:
        print("RCA Precision: PASSED")
        return True
    else:
        print("RCA Precision: FAILED")
        return False

def run_high_throughput_audit():
    print("Testing 10Gbps Aggregate Throughput...")
    # Run tsb with 4 threads
    res = subprocess.run(["sudo", "./build/tsb"], capture_output=True, text=True)
    print(res.stdout)
    if "PASSED" in res.stdout:
        print("10Gbps Aggregate Throughput: PASSED")
        return True
    else:
        print("10Gbps Aggregate Throughput: FAILED")
        return False

def main():
    print("=== TsAnalyzer Pro: Final Roadmap Validation ===")
    
    report = {
        "timestamp": time.ctime(),
        "tests": []
    }

    tests = [
        ("RST Accuracy", run_accuracy_test),
        ("RCA Precision", run_rca_precision_test),
        ("10Gbps Throughput & Zero-Malloc", run_high_throughput_audit)
    ]

    all_passed = True
    for name, test_func in tests:
        passed = test_func()
        report["tests"].append({"name": name, "passed": passed})
        if not passed: all_passed = False

    with open(RESULTS_FILE, "w") as f:
        json.dump(report, f, indent=2)

    print("--------------------------------------------------")
    if all_passed:
        print("FINAL RESULT: ALL ROADMAP GATES PASSED")
    else:
        print("FINAL RESULT: ROADMAP VALIDATION FAILED")
        exit(1)

if __name__ == "__main__":
    main()
