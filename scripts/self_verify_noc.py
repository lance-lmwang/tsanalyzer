import urllib.request
import urllib.parse
import json
import sys
import time

def query_prometheus(query):
    url = f"http://127.0.0.1:9090/api/v1/query?query={urllib.parse.quote(query)}"
    try:
        with urllib.request.urlopen(url, timeout=5) as r:
            return json.loads(r.read().decode())
    except:
        return None

def check_labels():
    print("[1/4] Checking label consistency...")
    url = "http://127.0.0.1:9090/api/v1/label/stream_id/values"
    try:
        with urllib.request.urlopen(url) as r:
            ids = json.loads(r.read().decode())['data']
            # Filter for STR- style labels to avoid mock junk
            str_ids = [i for i in ids if i.startswith("STR-")]
            print(f"  - Found {len(str_ids)} professional stream_ids.")
            if len(str_ids) < 16:
                print(f"  - ERROR: Expected 16 STR- streams, found {len(str_ids)}")
                return False
    except Exception as e:
        print(f"  - ERROR: Prometheus unreachable: {e}")
        return False
    return True

def check_metrics():
    print("[2/4] Checking metric integrity...")
    metrics = ["tsa_health_score", "tsa_total_packets", "tsa_pid_bitrate_bps"]
    for m in metrics:
        data = query_prometheus(f"count({m})")
        if data and data['data']['result']:
            val = data['data']['result'][0]['value'][1]
            print(f"  - Metric {m:25}: OK (Found {val} series)")
        else:
            print(f"  - Metric {m:25}: FAILED (No data)")
            return False
    return True

def check_dashboard_match():
    print("[3/4] Checking Dashboard-to-Backend contract...")
    try:
        with open("monitoring/grafana/provisioning/dashboards/tsa_pro_noc.json", "r") as f:
            db = json.load(f)
        content = json.dumps(db)
        if "stream_id=" not in content:
            print("  - ERROR: Dashboard is not using 'stream_id' filters")
            return False
        print("  - Data Contract Alignment: OK")
        return True
    except Exception as e:
        print(f"  - ERROR: Dashboard JSON unreadable: {e}")
        return False

def run_all():
    print("\n=== TSA PRO: NOC SELF-VERIFICATION SUITE ===\n")
    success = True
    if not check_labels(): success = False
    if not check_metrics(): success = False
    if not check_dashboard_match(): success = False

    if success:
        print("\n[RESULT] ALL SYSTEMS NORMAL. Ready for drill-down.")
        sys.exit(0)
    else:
        print("\n[RESULT] SYSTEM VERIFICATION FAILED.")
        sys.exit(1)

if __name__ == "__main__":
    run_all()
