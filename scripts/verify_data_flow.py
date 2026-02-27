import urllib.request
import urllib.parse
import json
import time
import sys

def get_json(url):
    try:
        with urllib.request.urlopen(url, timeout=5) as r:
            return json.loads(r.read().decode())
    except Exception as e:
        return None

def check_pipeline():
    print("=== TSA PRO: DATA FLOW PIPELINE AUDIT ===\n")
    
    # 1. 检查后端指标产出
    print("[1/4] Checking TSA Server Metrics (/metrics)...")
    try:
        with urllib.request.urlopen("http://127.0.0.1:8080/metrics", timeout=2) as r:
            content = r.read().decode()
            if 'stream_id="STR-1"' in content:
                print("  - [OK] STR-1 metrics found in raw output.")
            else:
                print("  - [FAIL] STR-1 NOT FOUND.")
                return False
    except:
        print("  - [FAIL] Cannot connect to port 8080.")
        return False

    # 2. 检查 Prometheus 采集状态
    print("[2/4] Checking Prometheus Scrape Status...")
    targets = get_json("http://127.0.0.1:9090/api/v1/targets")
    if not targets:
        print("  - [FAIL] Prometheus API unreachable.")
        return False
    
    found_up = False
    for t in targets['data']['activeTargets']:
        if t['labels'].get('instance') == "PRO-STABILITY-TEST":
            if t['health'] == "up":
                print(f"  - [OK] Target is UP.")
                found_up = True
            else:
                print(f"  - [FAIL] Target health: {t['health']}")
                return False
    if not found_up:
        print("  - [FAIL] Target not in Prometheus.")
        return False

    # 3. 检查 Prometheus 数据库
    print("[3/4] Checking Prometheus DB...")
    query = 'tsa_health_score{stream_id="STR-1"}'
    url = f"http://127.0.0.1:9090/api/v1/query?query={urllib.parse.quote(query)}"
    db_res = get_json(url)
    if db_res and db_res['data']['result']:
        print(f"  - [OK] Data found in DB.")
    else:
        print("  - [FAIL] NO DATA IN DB. Label mismatch?")
        return False

    return True

if __name__ == "__main__":
    if check_pipeline():
        print("\n[RESULT] SUCCESS")
        sys.exit(0)
    else:
        print("\n[RESULT] FAILURE")
        sys.exit(1)
