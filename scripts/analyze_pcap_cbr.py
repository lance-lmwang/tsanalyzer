import sys
import os

def analyze_cbr(pcap_file, target_mbps):
    # This is a lightweight PCAP analyzer using raw file reading to avoid scapy dependencies
    # It assumes standard UDP/TS packets
    print(f">>> PCAP Analysis: Target {target_mbps} Mbps")
    # For now, we use a simpler approach: process the server's own arrival logs if needed
    # But to fulfill the user request, we'll suggest using tshark for deep analysis
    cmd = f"tshark -r {pcap_file} -T fields -e frame.time_relative -e frame.len > times.txt 2>/dev/null"
    print(f"Executing: {cmd}")
    os.system(cmd)

    if not os.path.exists("times.txt"):
        print("Error: tshark not found or failed to process.")
        return

    with open("times.txt", "r") as f:
        lines = f.readlines()

    # Bucket packets into 10ms windows
    buckets = {}
    for line in lines:
        parts = line.split()
        if len(parts) < 2: continue
        t = float(parts[0])
        l = int(parts[1])
        bucket_idx = int(t * 100) # 100 buckets per second = 10ms
        buckets[bucket_idx] = buckets.get(bucket_idx, 0) + l

    if not buckets: return

    bitrates = [ (b * 8 / 0.01) / 1e6 for b in buckets.values() ]
    avg = sum(bitrates) / len(bitrates)
    max_v = max(bitrates)
    min_v = min(bitrates)

    print(f"--- 10ms Window Statistics ---")
    print(f"Average Bitrate: {avg:.2f} Mbps")
    print(f"Peak (10ms):    {max_v:.2f} Mbps")
    print(f"Floor (10ms):   {min_v:.2f} Mbps")
    print(f"Stability (StdDev-like): {max_v - min_v:.2f} Mbps")

    if (max_v - avg) < (avg * 0.2): # Less than 20% burst
        print(">>> RESULT: 10ms CBR STABILITY PASSED")
    else:
        print(">>> RESULT: 10ms CBR STABILITY FAILED (Micro-bursts detected)")

if __name__ == "__main__":
    if len(sys.argv) > 2:
        analyze_cbr(sys.argv[1], sys.argv[2])
