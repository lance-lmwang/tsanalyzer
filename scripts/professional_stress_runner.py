#!/usr/bin/env python3
import time, requests, subprocess, os, sys

# Professional Configuration
API_PORT = 8100
UDP_START_PORT = 12345
STREAMS = 7
DURATION = 300
REPORT_FILE = "/tmp/stability_heartbeat.csv"

def cleanup():
    subprocess.run(f"fuser -k -9 {API_PORT}/tcp", shell=True, stderr=subprocess.DEVNULL)
    for i in range(STREAMS):
        subprocess.run(f"fuser -k -9 {UDP_START_PORT+i}/udp", shell=True, stderr=subprocess.DEVNULL)
    subprocess.run("pkill -9 tsa_server", shell=True, stderr=subprocess.DEVNULL)
    subprocess.run("pkill -9 tsp", shell=True, stderr=subprocess.DEVNULL)
    time.sleep(2)

def run():
    cleanup()
    print(f"[*] Starting v10.1 High-Resilience Server...")
    server_proc = subprocess.Popen(["./build/tsa_server"], stdout=open("/tmp/server_stdout.log", "w"), stderr=subprocess.STDOUT)
    
    # Wait for API to respond
    for _ in range(10):
        try:
            if requests.get(f"http://localhost:{API_PORT}/metrics", timeout=1).status_code == 200:
                print("[*] API is responsive.")
                break
        except: time.sleep(1)
    else:
        print("[-] Server failed to initialize.")
        return

    print(f"[*] Injecting {STREAMS} PCR-Locked streams...")
    for i in range(STREAMS):
        core = i + 1
        port = UDP_START_PORT + i
        cmd = ["taskset", "-c", str(core), "./build/tsp", "-P", "-l", "-t", "7", "-i", "127.0.0.1", "-p", str(port), "-f", "/home/lmwang/dev/sample/cctvhd.ts"]
        subprocess.Popen(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

    print(f"[*] Production Acceptance Test starting. Logging to {REPORT_FILE}")
    with open(REPORT_FILE, "w") as f:
        f.write("timestamp,elapsed,total_cc,mbps_total
")
        
    start_time = time.time()
    initial_cc = -1
    
    try:
        while time.time() - start_time < DURATION:
            time.sleep(5)
            elapsed = int(time.time() - start_time)
            try:
                r = requests.get(f"http://localhost:{API_PORT}/metrics", timeout=2)
                lines = r.text.splitlines()
                curr_cc = 0
                total_mbps = 0
                for line in lines:
                    if "tsa_cc" in line: curr_cc += int(float(line.split()[-1]))
                    if "tsa_mbps" in line: total_mbps += float(line.split()[-1])
                
                if initial_cc == -1: initial_cc = curr_cc
                delta = curr_cc - initial_cc
                
                with open(REPORT_FILE, "a") as f:
                    f.write(f"{time.time()},{elapsed},{curr_cc},{total_mbps}
")
                
                if delta > 0:
                    print(f"
[!] ALERT: CC ERROR DETECTED! Delta: {delta}")
            except Exception as e:
                print(f"
[!] API HANG: {e}")
    finally:
        cleanup()

if __name__ == "__main__":
    run()
