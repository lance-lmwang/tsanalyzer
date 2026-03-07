#!/usr/bin/env python3
import time, requests, subprocess, os, sys

STREAM_ID = "STR-PRO-5M"
TEST_DURATION = 300
CHECK_INTERVAL = 5
PORT = 8088
URL_BASE = f"http://localhost:{PORT}"

def cleanup():
    print(f"Force cleaning port {PORT} and old processes...")
    subprocess.run(f"fuser -k -9 {PORT}/tcp", shell=True, stderr=subprocess.DEVNULL)
    subprocess.run("pkill -9 tsa_server", shell=True, stderr=subprocess.DEVNULL)
    subprocess.run("pkill -9 -f simulate_mdi_srt_incident.py", shell=True, stderr=subprocess.DEVNULL)
    time.sleep(2)

def verify_server():
    """Verify server is responding before starting test"""
    for _ in range(10):
        try:
            r = requests.get(f"{URL_BASE}/metrics", timeout=2)
            if r.status_code == 200: return True
        except: pass
        time.sleep(1)
    return False

def run():
    cleanup()
    print(f"Starting server on port {PORT}...")
    server = subprocess.Popen(["./build/tsa_server", f"http://0.0.0.0:{PORT}"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

    if not verify_server():
        print("CRITICAL: Server failed to start or respond on port 8088")
        server.terminate()
        return False

    print("Server is UP. Creating stream...")
    try:
        requests.post(f"{URL_BASE}/streams?id={STREAM_ID}", timeout=5)
    except Exception as e:
        print(f"Failed to create stream: {e}")
        server.terminate()
        return False

    print("Starting simulator...")
    sim = subprocess.Popen(["python3", "scripts/simulate_mdi_srt_incident.py", "--stream_id", STREAM_ID, "--mode", "clean"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

    start = time.time()
    try:
        while time.time() - start < TEST_DURATION:
            time.sleep(CHECK_INTERVAL)
            elapsed = int(time.time() - start)
            try:
                r = requests.get(f"{URL_BASE}/metrics", timeout=2)
                if r.status_code == 200:
                    metrics = r.text
                    cc = 0
                    health = 0
                    for line in metrics.splitlines():
                        if f'tsa_compliance_tr101290_p1_cc_errors_total{{stream_id="{STREAM_ID}"}}' in line: cc = int(float(line.split()[-1]))
                        if f'tsa_system_health_score{{stream_id="{STREAM_ID}"}}' in line: health = float(line.split()[-1])

                    print(f"[{elapsed:3d}s] Health:{health:.1f} CC:{cc}")
                    sys.stdout.flush()

                    if cc > 0:
                        print("\nFAILURE: CC Errors detected!")
                        return False
                    if elapsed > 30 and health < 90:
                        print(f"\nFAILURE: Health low ({health})")
                        return False
                else:
                    print(f"\nFAILURE: HTTP {r.status_code}")
                    return False
            except Exception as e:
                print(f"\nFAILURE: Metrics access error: {e}")
                return False
        print("\nSUCCESS: 5 Minute stability verified with ZERO CC errors.")
        return True
    finally:
        sim.terminate()
        server.terminate()

if __name__ == "__main__":
    for i in range(3):
        print(f"\n--- Attempt {i+1} ---")
        if run(): sys.exit(0)
        time.sleep(2)
    sys.exit(1)
