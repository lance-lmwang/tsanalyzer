import subprocess
import time
import requests
import os

# TsAnalyzer Stable Release Drive
SERVER_BIN = os.path.abspath("./build/tsa_server")
PACER_BIN = os.path.abspath("./build/tsp")
SAMPLE_TS = next((f for f in ["./sample/test.ts", "../sample/test.ts", "/home/lmwang/dev/sample/test.ts"] if os.path.exists(f)), "/home/lmwang/dev/sample/test.ts")

print(">>> Release Validation: Starting Server...")
srv = subprocess.Popen([SERVER_BIN], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
time.sleep(3)

print(">>> Release Validation: Starting Pacer (Locked to PCR)...")
pac = subprocess.Popen([PACER_BIN, "-P", "-l", "-m", "0x0202", "-i", "127.0.0.1", "-p", "19001", "-f", SAMPLE_TS],
                       stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)

print(">>> Release Validation: Monitoring Metrics (20s)...")
print("%-10s | %-12s | %-10s | %-10s" % ("Elapsed", "Phys Mbps", "PCR Mbps", "Jitter ms"))
print("-" * 50)

start_t = time.time()
for _ in range(4):
    time.sleep(5)
    try:
        r = requests.get("http://127.0.0.1:8088/metrics", timeout=2)
        m = r.text
        phys = 0
        pcr = 0
        jitter = 0

        for line in m.splitlines():
            if 'stream_id="STR-1"' in line:
                val = float(line.split()[-1])
                if "tsa_metrology_physical_bitrate_bps" in line: phys = val
                if "tsa_metrology_pcr_bitrate_bps" in line: pcr = val
                if "tsa_metrology_pcr_jitter_ms" in line: jitter = val

        elapsed = time.time() - start_t
        print("%-10.1fs | %-12.2f | %-10.2f | %-10.3f" % (elapsed, phys/1e6, pcr/1e6, jitter))
    except Exception as e:
        print("Error fetching metrics:", e)

srv.terminate()
pac.terminate()
print(">>> Release Validation: Complete.")
