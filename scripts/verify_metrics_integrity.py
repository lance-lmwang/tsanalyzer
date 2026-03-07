import urllib.request
import sys
from collections import defaultdict

def verify():
    print("=== Metrics Uniqueness and Integrity Audit ===")
    try:
        response = urllib.request.urlopen("http://localhost:8088/metrics")
        content = response.read().decode('utf-8')
    except Exception as e:
        print(f"FAILED: Cannot access metrics endpoint: {e}")
        return False

    lines = content.splitlines()
    metrics_map = defaultdict(list)
    errors = 0

    for line in lines:
        if line.startswith("#") or not line.strip():
            continue

        # Parse metric names and labels
        parts = line.split('{')
        name = parts[0]
        label_part = parts[1].split('}')[0] if len(parts) > 1 else ""

        # Record all label sets for this metric
        metrics_map[name].append(label_part)

    # Check uniqueness of core dashboard metrics
    critical_metrics = [
        "tsa_system_signal_locked",
        "tsa_system_health_score",
        "tsa_rst_encoder_seconds",
        "tsa_essence_video_fps"
    ]

    for m in critical_metrics:
        labels = metrics_map.get(m, [])
        if not labels:
            print(f"[ERROR] Missing critical metric: {m}")
            errors += 1
            continue

        # Check for each stream_id
        stream_ids = defaultdict(int)
        for lp in labels:
            sid = ""
            for pair in lp.split(','):
                if 'stream_id=' in pair:
                    sid = pair.split('=')[1].strip('"')
            if sid:
                stream_ids[sid] += 1

        for sid, count in stream_ids.items():
            if count > 1:
                print(f"[CRITICAL] Metric duplicated: {m} for {sid} appeared {count} times!")
                errors += 1
            else:
                print(f"[PASS] {m} ({sid}) uniqueness verified")

    if errors == 0:
        print(">>> Audit Passed: All core metrics are globally unique and aligned with dashboard.")
        return True
    else:
        print(f">>> Audit Failed: Found {errors} serious conflicts.")
        return False

if __name__ == "__main__":
    if not verify():
        sys.exit(1)
