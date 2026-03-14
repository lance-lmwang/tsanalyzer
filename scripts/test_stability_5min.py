#!/usr/bin/env python3
import time
import requests
import subprocess
import os
import sys

# CONFIGURATION
STREAM_ID = "STR-PRO-5M"
TEST_DURATION_SEC = 300  # 5 Minutes
CHECK_INTERVAL_SEC = 5
METRICS_URL = "http://localhost:8088/metrics"

def get_metric(metrics_text, name, stream_id):
    for line in metrics_text.splitlines():
        if line.startswith(name) and f'stream_id="{stream_id}"' in line:
            return float(line.split()[-1])
    return 0.0

def self_heal_env():
    """Environment Pre-check and repair"""
    print("Self-Healing Environment...")
    # 1. Check if binary exists, build if missing
    if not os.path.exists("./build/tsa_server"):
        print("WARNING: tsa_server binary missing. Building now...")
        subprocess.run(["./build.sh"], check=True)

    # 2. Cleanup old processes
    subprocess.run(["pkill", "-9", "tsa_server"], stderr=subprocess.DEVNULL)
    subprocess.run(["pkill", "-9", "-f", "simulate_mdi_srt_incident.py"], stderr=subprocess.DEVNULL)
    time.sleep(1)

def run_test_session():
    self_heal_env()

    print(f"Starting 5-minute Industrial Stability Test for {STREAM_ID}")

    # 2. Start TSA Server in background
    log_f = open("test_e2e_server.log", "w")
    server_proc = subprocess.Popen(["./build/tsa_server", "http://0.0.0.0:8088"], stdout=log_f, stderr=log_f)
    time.sleep(2)

    # 3. Create Stream Task
    try:
        print(f"Creating Stream Task: {STREAM_ID}")
        requests.post(f"http://localhost:8088/streams?id={STREAM_ID}", timeout=5)
    except Exception as e:
        print(f"ERROR: Failed to reach server to create stream: {e}")
        return False

    # 4. Start Traffic Simulator
    print("Injecting High-Precision SRT Stream...")
    sim_proc = subprocess.Popen(["python3", "scripts/simulate_mdi_srt_incident.py", "--stream_id", STREAM_ID, "--mode", "clean"])

    start_time = time.time()

    try:
        while time.time() - start_time < TEST_DURATION_SEC:
            elapsed = int(time.time() - start_time)
            errors = []
            try:
                resp = requests.get(METRICS_URL, timeout=2)
                if resp.status_code == 200:
                    metrics = resp.text

                    cc_errors = get_metric(metrics, "tsa_tr101290_p1_cc_errors_total", STREAM_ID)
                    health = get_metric(metrics, "tsa_system_health_score", STREAM_ID)
                    bitrate = get_metric(metrics, "tsa_metrology_physical_bitrate_bps", STREAM_ID)
                    fps = get_metric(metrics, "tsa_essence_video_fps", STREAM_ID)

                    status = f"[{elapsed:3d}s] Health:{health:.1f} CC:{int(cc_errors)} Bitrate:{bitrate/1e6:.2f}M FPS:{fps:.1f}"
                    print(status)
                    sys.stdout.flush()

                    # HARD ASSERTIONS
                    if cc_errors > 0:
                        errors.append(f"CRITICAL: CC error detected! ({cc_errors})")
                    if health < 95:
                        errors.append(f"WARNING: Health score too low ({health})")
                    if bitrate < 500000:
                        errors.append(f"CRITICAL: Loss of signal (Bitrate={bitrate})")

                else:
                    errors.append(f"ERROR: Server responded {resp.status_code}")
            except Exception as e:
                errors.append(f"ERROR: Failed to fetch metrics: {e}")

            if errors:
                print(f"\nSESSION FAILED at {elapsed}s:")
                for e in errors: print(f"  - {e}")
                return False

            time.sleep(CHECK_INTERVAL_SEC)

        print(f"\n5-MINUTE STABILITY TEST PASSED FOR {STREAM_ID}!")
        return True

    finally:
        sim_proc.terminate()
        server_proc.terminate()
        time.sleep(1)

if __name__ == "__main__":
    attempt = 1
    max_attempts = 10

    while attempt <= max_attempts:
        print(f"\n--- [ATTEMPT {attempt}/{max_attempts}] ---")
        if run_test_session():
            print("Final Result: SUCCESS")
            sys.exit(0)
        else:
            print(f"WARNING: Session {attempt} failed. Retrying in 5 seconds...")
            attempt += 1
            time.sleep(5)

    print("FAILED AFTER MAXIMUM RETRIES.")
    sys.exit(1)
