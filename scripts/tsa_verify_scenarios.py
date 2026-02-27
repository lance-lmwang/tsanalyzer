#!/usr/bin/env python3
import argparse
import time
import urllib.request
import sys

def get_metrics(url):
    try:
        with urllib.request.urlopen(url) as response:
            return response.read().decode('utf-8')
    except Exception as e:
        print(f"Error polling metrics: {e}")
        return None

def parse_metric(metrics, name, labels=None):
    if not metrics:
        return None
    for line in metrics.splitlines():
        if line.startswith(name):
            # Very simple parser for now
            if " " in line:
                val_str = line.rsplit(" ", 1)[1]
                try:
                    return float(val_str)
                except ValueError:
                    continue
    return None

def main():
    parser = argparse.ArgumentParser(description="TSA Chaos Scenario Verifier")
    parser.add_argument("--url", default="http://localhost:8000/metrics", help="Prometheus metrics URL")
    parser.add_argument("--scenario", choices=["loss", "jitter", "compound"], help="Pre-defined scenario to verify")
    parser.add_argument("--duration", type=int, default=10, help="Duration to poll (seconds)")
    
    args = parser.parse_args()

    scenarios = {
        "loss": {
            "metric": "tsa_continuity_errors_total",
            "threshold": 0, # Should increase
            "mode": "increase"
        },
        "jitter": {
            "metric": "tsa_pcr_jitter_us",
            "threshold": 5000, # 5ms
            "mode": "max"
        },
        "compound": {
            "metric": "tsa_predictive_fault_domain",
            "threshold": 0, # 0 is OK, >0 is fault
            "mode": "max"
        }
    }

    if args.scenario:
        s = scenarios[args.scenario]
        args.metric = s["metric"]
        args.threshold = s["threshold"]
        mode = s["mode"]
    elif not args.metric:
        parser.print_help()
        sys.exit(0)
    else:
        mode = "max"

    start_time = time.time()
    initial_val = None
    max_val = -float('inf')
    passed = False

    while time.time() - start_time < args.duration:
        metrics = get_metrics(args.url)
        val = parse_metric(metrics, args.metric)
        if val is not None:
            if initial_val is None:
                initial_val = val
            
            max_val = max(max_val, val)
            print(f"Current {args.metric}: {val}")
            
            if mode == "increase":
                if val > initial_val:
                    passed = True
            elif mode == "max":
                if val > args.threshold:
                    passed = True
        
        if passed and mode == "increase":
            # For increase, we can stop early if we saw it happen
            # Actually better to keep polling to see total impact
            pass
            
        time.sleep(1)

    if passed:
        print(f"RESULT: PASSED (Scenario verified)")
        sys.exit(0)
    else:
        print(f"RESULT: FAILED (Threshold not reached or metric didn't increase)")
        sys.exit(1)

if __name__ == "__main__":
    main()
