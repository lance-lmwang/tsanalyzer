import os
#!/usr/bin/env python3
import time, requests, subprocess, os, sys, json

STREAM_ID = "STR-PRO-5M"
TEST_DURATION = 300
CHECK_INTERVAL = 5
PORT = 8090
UDP_PORT = 19001
URL_BASE = f"http://localhost:{PORT}"
SAMPLE = next((f for f in ["./sample/test.ts", "../sample/test.ts", "/home/lmwang/dev/sample/test.ts"] if os.path.exists(f)), "/home/lmwang/dev/sample/test.ts")

def cleanup():
    subprocess.run(f"fuser -k -9 {PORT}/tcp", shell=True, stderr=subprocess.DEVNULL, stdout=subprocess.DEVNULL)
    subprocess.run(f"fuser -k -9 {UDP_PORT}/udp", shell=True, stderr=subprocess.DEVNULL, stdout=subprocess.DEVNULL)
    subprocess.run("pkill -9 tsa_server", shell=True, stderr=subprocess.DEVNULL)
    subprocess.run("pkill -9 tsp", shell=True, stderr=subprocess.DEVNULL)
    time.sleep(1)

def run():
    print("==================================================")
    print(" MUX DIRECTOR E2E TEST: 5-Min Industrial Stability")
    print(" (Core Affinity: Analyzer[C0], Pacer[C1])")
    print("==================================================")

    if not os.path.exists(SAMPLE):
        print(f"Error: Sample file {SAMPLE} not found.")
        return False

    cleanup()

    # Start Analyzer on Core 0
    print(f"[*] Starting tsa_server on Core 0...")
    server_cmd = ["taskset", "-c", "0", "./build/tsa_server", f"http://0.0.0.0:{PORT}"]
    server = subprocess.Popen(server_cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

    for _ in range(10):
        try:
            if requests.get(f"{URL_BASE}/metrics", timeout=1).status_code == 200:
                break
        except:
            time.sleep(0.5)
    else:
        print("Server failed to start.")
        return False

    print(f"[*] Server UP. Creating stream {STREAM_ID} via SRT...")
    # Using SRT Listener mode on server side
    payload = {"stream_id": STREAM_ID, "url": "srt://:9200?mode=listener"}
    try:
        requests.post(f"{URL_BASE}/api/v1/config/streams", json=payload, timeout=2)
    except Exception as e:
        print(f"API Error: {e}")
        return False

    # Start Pacer as SRT Caller (No Loop to avoid discontinuity errors)
    print(f"[*] Starting TsPacer as SRT Caller (PCR-locked, Single Pass)...")
    tsp_cmd = [
        "taskset", "-c", "1", "./build/tsp", "-P",
        "--srt-url", "srt://127.0.0.1:9200?mode=caller", "-f", SAMPLE
    ]
    sim = subprocess.Popen(tsp_cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

    print(f"[*] E2E Test started. Polling every {CHECK_INTERVAL}s.")
    print("--------------------------------------------------")
    start = time.time()
    last_cc = -1
    try:
        while time.time() - start < TEST_DURATION:
            time.sleep(CHECK_INTERVAL)
            elapsed = int(time.time() - start)
            try:
                r = requests.get(f"{URL_BASE}/metrics", timeout=2)
                if r.status_code == 200:
                    lines = r.text.splitlines()
                    cc, bps, fps = 0, 0, 0
                    for line in lines:
                        if f'tsa_compliance_tr101290_p1_cc_errors_total{{stream_id="{STREAM_ID}"}}' in line: cc = int(float(line.split()[-1]))
                        if f'tsa_metrology_physical_bitrate_bps{{stream_id="{STREAM_ID}"}}' in line: bps = float(line.split()[-1])
                        if f'tsa_essence_video_fps{{stream_id="{STREAM_ID}"}}' in line: fps = float(line.split()[-1])

                    mbps = bps / 1_000_000
                    print(f"[{elapsed:3d}s] Bitrate: {mbps:>5.2f} Mbps | FPS: {fps:>4.1f} | CC Errors: {cc}")
                    sys.stdout.flush()

                    if last_cc == -1: last_cc = cc
                    elif cc > last_cc:
                        print(f"\\n[FAIL] FAILED: CC Errors INCREASED! ({last_cc} -> {cc})")
                        return False
                    if elapsed > 10 and mbps < 1.0:
                        print(f"\\n[FAIL] FAILED: Bitrate Drop detected!")
                        return False
                else:
                    return False
            except Exception as e:
                print(f"Metrics Error: {e}")
                return False

        print("--------------------------------------------------")
        print("[PASS] SUCCESS: 5 Minute stability verified (PCR-Locked).")
        return True
    finally:
        sim.terminate()
        server.terminate()
        cleanup()

if __name__ == "__main__":
    if run(): sys.exit(0)
    sys.exit(1)
