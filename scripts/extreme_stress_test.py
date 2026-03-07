#!/usr/bin/env python3
import time, requests, subprocess, sys

STREAMS = 8
DURATION = 30
PORT_API = 8099
SAMPLE = "../sample/test.ts"

def get_stats():
    try:
        r = requests.get(f"http://localhost:{PORT_API}/metrics", timeout=2)
        stats = {}
        for line in r.text.splitlines():
            if "tsa_cc" in line:
                sid = line.split('"')[1]
                val = int(float(line.split()[-1]))
                stats[sid] = val
        return stats
    except: return {}

def run_extreme_test():
    print(f"[RUN] MUX DIRECTOR EXTREME STRESS: {STREAMS} STREAMS")
    subprocess.run(f"fuser -k -9 {PORT_API}/tcp", shell=True, stderr=subprocess.DEVNULL)
    for i in range(STREAMS): subprocess.run(f"fuser -k -9 {8088+i}/udp", shell=True, stderr=subprocess.DEVNULL)

    server = subprocess.Popen(["./build/tsa_server"])
    time.sleep(3)

    pacers = []
    for i in range(STREAMS):
        p = subprocess.Popen(["./build/tsp", "-P", "-l", "-t", "7", "-i", "127.0.0.1", "-p", str(8088+i), "-f", SAMPLE], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        pacers.append(p)

    print("[*] All Pacers Active. Calibrating baseline...")
    time.sleep(5)
    baseline = get_stats()

    if not baseline:
        print("[FAIL] CRITICAL: Could not gather metrics.")
        return False

    print(f"[*] Baseline established. Monitoring for {DURATION}s...")
    start = time.time()
    while time.time() - start < DURATION:
        time.sleep(10)
        current = get_stats()
        for sid, val in current.items():
            if val > baseline.get(sid, 0):
                print(f"\n[FAIL] FAILURE: CC Error detected on STR-{sid}! ({baseline[sid]} -> {val})")
                return False
        print(f"    [{int(time.time()-start)}s] CC Stable...")

    print("\n[PASS] SUCCESS: 8-Stream Extreme Stress Test PASSED.")
    return True

if __name__ == "__main__":
    try:
        if run_extreme_test(): sys.exit(0)
    finally:
        subprocess.run("pkill -9 tsa_server", shell=True)
        subprocess.run("pkill -9 tsp", shell=True)
    sys.exit(1)
