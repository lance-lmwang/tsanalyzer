import os
#!/usr/bin/env python3
import time
import socket
import statistics
import sys

def measure_jitter(port, duration=10):
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind(('0.0.0.0', port))
    sock.settimeout(1.0)

    intervals = []
    last_time = None
    start_time = time.time()

    print(f"Measuring jitter on port {port} for {duration}s...")

    while time.time() - start_time < duration:
        try:
            data, addr = sock.recvfrom(2048)
            now = time.perf_counter()
            if last_time is not None:
                intervals.append((now - last_time) * 1000) # ms
            last_time = now
        except socket.timeout:
            continue

    if not intervals:
        print("No packets received")
        return

    avg = sum(intervals) / len(intervals)
    std_dev = statistics.stdev(intervals) if len(intervals) > 1 else 0
    max_val = max(intervals)
    min_val = min(intervals)

    print(f"\nResults:")
    print(f"  Packets: {len(intervals) + 1}")
    print(f"  Avg Interval: {avg:.3f} ms")
    print(f"  Std Dev:      {std_dev:.3f} ms (Jitter)")
    print(f"  Min/Max:      {min_val:.3f} / {max_val:.3f} ms")

    # Heuristic: For 10Mbps stream, 7 TS packets = 1316 bytes.
    # 10,000,000 bits/s = 1,250,000 bytes/s
    # Interval should be 1316 / 1,250,000 = 1.05 ms
    # If Jitter is < 20% of interval, it's good.
    if std_dev < avg * 0.5:
        print("\n[PASS] Pacing is stable.")
    else:
        print("\n[FAIL] High jitter detected!")

if __name__ == "__main__":
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 1234
    measure_jitter(port)
