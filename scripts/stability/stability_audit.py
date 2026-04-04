import os, import time
import requests
import re
import sys

# Configuration
URL = "http://localhost:8088/metrics"
DURATION_SEC = 600  # 10 minutes
INTERVAL_SEC = 5
SAMPLES = DURATION_SEC // INTERVAL_SEC

print(f"=== TsAnalyzer Stability Audit: 10 Minutes @ 5s intervals ===")
print(f"Monitoring 4 streams at {URL}")
print(f"{'Time':<10} | {'Stream':<8} | {'Bitrate':<12} | {'CC Err':<8} | {'Health':<8} | {'Jitter':<8}")
print("-" * 75)

def get_metrics():
    try:
        resp = requests.get(URL, timeout=2)
        content = resp.text
        results = {}

        # Extract metrics using regex
        # Pattern: metric_name{stream_id="STR-X"} value
        patterns = {
            'bitrate': r'tsa_metrology_physical_bitrate_bps\{stream_id="STR-(\d+)"\} ([\d\.]+)',
            'cc_err': r'tsa_tr101290_p1_cc_error\{stream_id="STR-(\d+)"\} ([\d\.]+)',
            'health': r'tsa_system_health_score\{stream_id="STR-(\d+)"\} ([\d\.]+)',
            'jitter': r'tsa_metrology_pcr_jitter_ms\{stream_id="STR-(\d+)"\} ([\d\.]+)'
        }

        for m_name, pat in patterns.items():
            matches = re.findall(pat, content)
            for sid, val in matches:
                if sid not in results: results[sid] = {}
                results[sid][m_name] = float(val)
        return results
    except Exception as e:
        print(f"Error fetching metrics: {e}")
        return None

# Audit Log
log_file = "stability_audit.log"
with open(log_file, "w") as f:
    f.write("Timestamp,Stream,Bitrate,CC_Errors,Health,Jitter\n")

for i in range(SAMPLES):
    data = get_metrics()
    timestamp = time.strftime("%H:%M:%S")

    if data:
        for sid in sorted(data.keys()):
            m = data[sid]
            # Output to console
            print(f"{timestamp:<10} | STR-{sid:<4} | {m['bitrate']:>10.0f} | {m['cc_err']:>8.0f} | {m['health']:>8.1f} | {m['jitter']:>8.3f}")

            # Save to log
            with open(log_file, "a") as f:
                f.write(f"{timestamp},STR-{sid},{m['bitrate']},{m['cc_err']},{m['health']},{m['jitter']}\n")

            # Immediate anomaly check
            if m['cc_err'] > 0:
                print(f"!! ANOMALY DETECTED: CC Error on Stream {sid} !!")
            if m['health'] < 80:
                print(f"!! ANOMALY DETECTED: Low Health on Stream {sid} !!")

    time.sleep(INTERVAL_SEC)

print("-" * 75)
print("Audit Complete. Final results saved to stability_audit.log")
