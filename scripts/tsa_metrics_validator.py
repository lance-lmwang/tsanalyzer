import urllib.request
import time
import sys

def get_metrics():
    try:
        with urllib.request.urlopen("http://127.0.0.1:8082/metrics", timeout=2) as response:
            return response.read().decode('utf-8')
    except Exception as e:
        print(f"ERROR: Failed to connect to API: {e}")
        return None

def parse_metric(data, name, stream_id):
    # Primitive parser for Prometheus format
    marker = f'{name}{{stream_id="{stream_id}"}}'
    for line in data.splitlines():
        if line.startswith(marker):
            return float(line.split()[-1])
    return None

import re

def run_audit():
    print("=== TSANALYZER PRO: AUTOMATED METRICS AUDIT ===")

    # Baseline
    print("Sampling baseline...")
    m1 = get_metrics()
    if not m1: return False

    time.sleep(5)

    # Comparison
    print("Sampling convergence...")
    m2 = get_metrics()
    if not m2: return False

    # Discover stream IDs from m2
    # Pattern: tsa_system_total_packets{stream_id="STREAM_ID"}
    streams = re.findall(r'tsa_system_total_packets\{stream_id="([^"]+)"\}', m2)
    if not streams:
        print("ERROR: No active streams found in metrics!")
        return False

    streams = sorted(list(set(streams)))
    print(f"Discovered {len(streams)} active streams.")

    all_pass = True
    print(f"{'Stream':<12} | {'Packets':<10} | {'Bitrate':<12} | {'Health':<8} | {'Status'}")
    print("-" * 65)

    for s in streams:
        p1 = parse_metric(m1, "tsa_system_total_packets", s)
        p2 = parse_metric(m2, "tsa_system_total_packets", s)
        br = parse_metric(m2, "tsa_metrology_physical_bitrate_bps", s)
        hl = parse_metric(m2, "tsa_system_health_score", s)

        # Validation Logic
        if p1 is None or p2 is None:
            status = "MISSING"
            all_pass = False
        elif p2 <= p1 and p2 > 0: # Check if stuck
            status = "STALLED"
            all_pass = False
        elif br is None or br < 10000: # Allow for low bitrate but not zero
            status = "LOW_BR"
            all_pass = False
        else:
            status = "OK"

        print(f"{s:<8} | {int(p2 if p2 else 0):<10} | {int(br if br else 0):<12} | {hl:<8} | {status}")

    return all_pass

if __name__ == "__main__":
    success = run_audit()
    if not success:
        print("\nAUDIT FAILED: One or more streams are abnormal.")
        sys.exit(1)
    else:
        print("\nAUDIT SUCCESS: All streams are healthy and reporting accurate metrics.")
        sys.exit(0)
