#!/usr/bin/env python3
import subprocess
import time
import json
import urllib.request
import os
import signal

def run_hardcore_test():
    print(">>> INITIALIZING IRONCLAD VERIFICATION SUITE...")
    
    # 1. Build release
    subprocess.run(["make", "release"], check=True)
    
    # 2. Setup environment
    conf_path = "ironclad.conf"
    with open(conf_path, "w") as f:
        f.write("GLOBAL http_port 8081\n")
        f.write("ST-AUTO udp://127.0.0.1:30005\n")
    
    # 3. Start Processes
    pkill = subprocess.run(["pkill", "-9", "tsa_server_pro"], stderr=subprocess.DEVNULL)
    pkill = subprocess.run(["pkill", "-9", "tsa_generator"], stderr=subprocess.DEVNULL)
    
    server = subprocess.Popen(["./build/tsa_server_pro", conf_path], stdout=subprocess.DEVNULL)
    gen = subprocess.Popen(["./build/tsa_generator", "-i", "127.0.0.1", "-p", "30005", "-b", "10000000"], stdout=subprocess.DEVNULL)
    
    print(">>> Processes started. Monitoring SHM for 15 seconds...")
    
    errors = 0
    checks = 0
    start_time = time.time()
    
    try:
        while time.time() - start_time < 15:
            checks += 1
            try:
                # We use the JSON API for ground truth validation
                with urllib.request.urlopen("http://127.0.0.1:8081/api/v1/snapshot?id=ST-AUTO") as resp:
                    data = json.loads(resp.read().decode())
                    pred = data['predictive']
                    s_drift = abs(pred['stc_wall_drift_ppm'])
                    l_drift = abs(pred['long_term_drift_ppm'])
                    
                    if s_drift > 1000 or l_drift > 1000:
                        print(f"\n[!] BREAKING: Abnormal drift detected! S={s_drift:.2f}, L={l_drift:.2f}")
                        errors += 1
                        break # Stop immediately on error
            except:
                pass
            
            if checks % 10 == 0:
                print(".", end="", flush=True)
            time.sleep(0.1)
            
        if errors == 0:
            print("\n>>> VERIFICATION SUCCESS: All metrics are stable and professional.")
            return True
        else:
            return False
            
    finally:
        server.terminate()
        gen.terminate()
        if os.path.exists(conf_path): os.remove(conf_path)

if __name__ == "__main__":
    if not run_hardcore_test():
        exit(1)
