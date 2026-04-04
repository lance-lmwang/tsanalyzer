#!/usr/bin/env python3
"""
MPEG-TS Elementary Stream (ES) Fluctuation Analyzer
Measures the bitrate stability of individual PIDs over small windows.
Compliant with TR 101 290 / T-STD expectations.
"""

import sys
import argparse
from collections import defaultdict

TS_PACKET_SIZE = 188
SYNC_BYTE = 0x47

def analyze_fluctuation(file_path, window_ms=100):
    print(f"[*] Analyzing Fluctuation: {file_path} (Window: {window_ms}ms)")

    # We need a way to estimate time if we don't have PCAP timestamps.
    # For a CBR file, we can estimate time based on packet count and total bitrate.
    # But wait, we might not know the total bitrate.
    # Let's assume the user provides the target bitrate or we estimate it from PCR.

    pid_data = defaultdict(list) # pid -> list of (offset, timestamp_estimate)

    file_size = 0
    with open(file_path, 'rb') as f:
        f.seek(0, 2)
        file_size = f.tell()
        f.seek(0)

        # 1. First pass: Find PCRs to estimate total CBR
        pcr_pid = None
        pcr_values = [] # (offset, pcr_ns)

        offset = 0
        while True:
            buf = f.read(TS_PACKET_SIZE * 1000)
            if not buf: break

            for i in range(0, len(buf), TS_PACKET_SIZE):
                pkt = buf[i:i+TS_PACKET_SIZE]
                if len(pkt) < TS_PACKET_SIZE or pkt[0] != SYNC_BYTE:
                    offset += TS_PACKET_SIZE
                    continue

                pid = ((pkt[1] & 0x1F) << 8) | pkt[2]

                # Check for PCR
                afc = (pkt[3] >> 4) & 0x03
                if afc >= 2 and pkt[4] >= 7 and (pkt[5] & 0x10):
                    import struct
                    pcr_h, pcr_l = struct.unpack(">IH", pkt[6:12])
                    pcr_base = (pcr_h << 1) | (pcr_l >> 15)
                    pcr_ext = pcr_l & 0x1FF
                    pcr_ns = (pcr_base * 300 + pcr_ext) * 1000 // 27

                    if pcr_pid is None: pcr_pid = pid
                    if pid == pcr_pid:
                        pcr_values.append((offset, pcr_ns))

                offset += TS_PACKET_SIZE

        if len(pcr_values) < 2:
            print("[!] Error: Not enough PCRs to estimate bitrate.")
            return

        # Estimate CBR
        dt = (pcr_values[-1][1] - pcr_values[0][1]) / 1e9
        db = (pcr_values[-1][0] - pcr_values[0][0]) * 8
        cbr_bps = db / dt
        print(f"[*] Estimated Total CBR: {cbr_bps/1e6:.4f} Mbps")

        # 2. Second pass: Collect PID statistics
        f.seek(0)
        offset = 0
        pid_counts = defaultdict(int) # pid -> total_bits
        pid_window_bits = defaultdict(lambda: defaultdict(int)) # pid -> window_idx -> bits

        window_ns = window_ms * 1_000_000

        while True:
            buf = f.read(TS_PACKET_SIZE * 1000)
            if not buf: break

            for i in range(0, len(buf), TS_PACKET_SIZE):
                pkt = buf[i:i+TS_PACKET_SIZE]
                if len(pkt) < TS_PACKET_SIZE or pkt[0] != SYNC_BYTE:
                    offset += TS_PACKET_SIZE
                    continue

                pid = ((pkt[1] & 0x1F) << 8) | pkt[2]

                # Estimate time of this packet
                # t = t0 + (offset - offset0) * 8 / cbr_bps
                t_ns = pcr_values[0][1] + int((offset - pcr_values[0][0]) * 8 * 1e9 / cbr_bps)

                window_idx = t_ns // window_ns
                pid_window_bits[pid][window_idx] += 188 * 8
                pid_counts[pid] += 188 * 8

                offset += TS_PACKET_SIZE

    # 3. Analyze Results
    print("\n" + "="*80)
    print(f"{'PID':<10} | {'Avg Rate':<15} | {'Std Dev':<15} | {'Max-Min':<15} | {'CV (%)':<10}")
    print("-"*80)

    for pid, windows in sorted(pid_window_bits.items()):
        if pid == 0x1FFF: continue # Ignore NULL

        rates = []
        # Ignore the very last window as it's often incomplete
        max_w = max(windows.keys())
        for w_idx in range(min(windows.keys()), max_w):
            bits = windows.get(w_idx, 0)
            rate = bits / (window_ms / 1000.0)
            rates.append(rate)

        if not rates: continue

        avg_rate = sum(rates) / len(rates)
        if avg_rate < 1000: continue # Ignore very low rate PIDs (PSI etc)

        variance = sum((r - avg_rate)**2 for r in rates) / len(rates)
        std_dev = variance**0.5
        cv = (std_dev / avg_rate) * 100 if avg_rate > 0 else 0
        max_rate = max(rates)
        min_rate = min(rates)

        print(f"0x{pid:04X}     | {avg_rate/1e6:10.4f} Mbps | {std_dev/1e6:10.4f} Mbps | {(max_rate-min_rate)/1e6:10.4f} Mbps | {cv:6.2f}%")

    print("="*80)
    print("CV: Coefficient of Variation (lower is better/smoother).")

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="MPEG-TS Fluctuation Analyzer")
    parser.add_argument("file", help="Input TS file")
    parser.add_argument("-w", "--window", type=int, default=100, help="Window size in ms (default: 100)")
    args = parser.parse_args()

    analyze_fluctuation(args.file, args.window)
