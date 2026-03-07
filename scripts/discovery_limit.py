#!/usr/bin/env python3
import time, requests, subprocess, sys

PORT_API = 8100
MAX_STREAMS = 7
SAMPLE = "../sample/test.ts"

def cleanup():
    subprocess.run("pkill -9 tsa_server; pkill -9 tsp; fuser -k -9 8100/tcp", shell=True, stderr=subprocess.DEVNULL)
    time.sleep(2)

def run_limit_discovery():
    cleanup()
    print("[RUN] STARTING STEPPED SCALING DISCOVERY")
    server = subprocess.Popen(["./build/tsa_server"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    time.sleep(3)

    for count in range(1, MAX_STREAMS + 1):
        print(f"--- Adding Stream #{count} ---")
        port = 8088 + count - 1
        core = count
        subprocess.Popen(["taskset", "-c", str(core), "./build/tsp", "-P", "-l", "-t", "7", "-i", "127.0.0.1", "-p", str(port), "-f", SAMPLE], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

        time.sleep(5)

        start_api = time.time()
        try:
            r = requests.get(f"http://localhost:{PORT_API}/metrics", timeout=3)
            latency = (time.time() - start_api) * 1000
            if r.status_code == 200:
                cc_total = 0
                for line in r.text.splitlines():
                    if "tsa_cc" in line:
                        try: cc_total += int(float(line.split()[-1]))
                        except: pass
                print(f"[{count} Streams] API Latency: {latency:.1f}ms | Total CC: {cc_total}")
            else:
                print(f"[{count} Streams] API Failed: {r.status_code}")
        except Exception as e:
            print(f"[{count} Streams] API HANG!")
            break

    print("--- Discovery Complete ---")
    cleanup()

if __name__ == "__main__":
    run_limit_discovery()
