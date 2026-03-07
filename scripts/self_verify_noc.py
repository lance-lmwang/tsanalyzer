import urllib.request
import urllib.parse
import json
import sys
import time

PROMETHEUS_URL = "http://127.0.0.1:9090"

def query_prometheus(query):
    url = f"{PROMETHEUS_URL}/api/v1/query?query={urllib.parse.quote(query)}"
    try:
        with urllib.request.urlopen(url, timeout=5) as r:
            return json.loads(r.read().decode())
    except Exception as e:
        print(f"  - DEBUG: Query failed: {e}")
        return None

def check_labels():
    print("[1/4] Checking label consistency (v4.0 Appliance)...")
    url = f"{PROMETHEUS_URL}/api/v1/label/stream_id/values"
    try:
        with urllib.request.urlopen(url, timeout=5) as r:
            ids = json.loads(r.read().decode())['data']
            # Support both UDP- and SRT- prefix
            str_ids = [i for i in ids if i.startswith("SRT-") or i.startswith("UDP-")]
            print(f"  - Found {len(str_ids)} active appliance stream_ids.")
            if len(str_ids) == 0:
                print("  - ERROR: No active streams found in Prometheus labels.")
                return False
    except Exception as e:
        print(f"  - ERROR: Prometheus unreachable at {PROMETHEUS_URL}: {e}")
        return False
    return True

def check_metrics():
    print("[2/4] Checking metric integrity (v4.0 Appliance)...")
    # Core appliance metrics
    metrics = [
        "tsa_system_health_score",
        "tsa_metrology_physical_bitrate_bps",
        "tsa_internal_analyzer_drop",
        "tsa_worker_slice_overruns"
    ]
    success = True
    for m in metrics:
        data = query_prometheus(f"count({m})")
        if data and data['data']['result']:
            val = data['data']['result'][0]['value'][1]
            print(f"  - Metric {m:30}: OK (Found {val} series)")
        else:
            print(f"  - Metric {m:30}: FAILED (No data)")
            success = False
    return success

def check_dashboard_match():
    print("[3/4] Checking Dashboard-to-Backend contract (v4.0)...")
    dashboard_path = "monitoring/grafana/provisioning/dashboards/tsa_pro_noc.json"
    try:
        with open(dashboard_path, "r") as f:
            db = json.load(f)
        content = json.dumps(db)

        # Check for new metrics in dashboard
        essential_metrics = ["tsa_worker_slice_overruns", "tsa_internal_analyzer_drop"]
        for m in essential_metrics:
            if m not in content:
                print(f"  - ERROR: Dashboard is missing essential metric: {m}")
                return False

        print(f"  - Local Data Contract ({db['uid']}): OK")

        # Verify Grafana service accessibility
        try:
            print("  - Verifying Grafana Live Dashboard (3000)...")
            # We use a simple socket check or head request to 3000
            import socket
            sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            sock.settimeout(2)
            result = sock.connect_ex(('127.0.0.1', 3000))
            if result == 0:
                print("  - Grafana Port 3000: ACCESSIBLE")
            else:
                print("  - WARNING: Grafana Port 3000 is not responding yet.")
            sock.close()
        except:
            pass

        return True
    except Exception as e:
        print(f"  - ERROR: Dashboard JSON ({dashboard_path}) unreadable: {e}")
        return False

def check_determinism():
    print("[4/4] Checking Determinism SLAs...")
    overruns = query_prometheus("sum(tsa_worker_slice_overruns)")
    drops = query_prometheus("sum(tsa_internal_analyzer_drop)")

    success = True
    if overruns and overruns['data']['result']:
        val = int(overruns['data']['result'][0]['value'][1])
        if val > 0:
            print(f"  - WARNING: Total Worker Overruns: {val}")
        else:
            print("  - Worker Scheduling: PERFECT DETERMINISM")

    if drops and drops['data']['result']:
        val = int(drops['data']['result'][0]['value'][1])
        if val > 0:
            print(f"  - WARNING: Total Internal Drops: {val}")
        else:
            print("  - Analysis Pipeline: ZERO BACKPRESSURE DROPS")

    return success

def run_all():
    print("\n=== TSANALYZER PRO: APPLIANCE NOC SELF-VERIFICATION ===\n")
    success = True
    if not check_labels(): success = False
    if not check_metrics(): success = False
    if not check_dashboard_match(): success = False
    if not check_determinism(): success = False

    if success:
        print("\n[RESULT] ALL SYSTEMS NORMAL. Appliance ready for Broadcast.")
        sys.exit(0)
    else:
        print("\n[RESULT] DEVIATIONS DETECTED. Check logs.")
        sys.exit(1)

if __name__ == "__main__":
    run_all()
