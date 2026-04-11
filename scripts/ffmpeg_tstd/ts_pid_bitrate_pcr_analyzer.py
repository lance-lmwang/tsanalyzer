#!/usr/bin/env python3
import sys
import struct
import argparse

def parse_pcr(pkt):
    """Extract 27MHz PCR value from a 188-byte TS packet."""
    afc = (pkt[3] >> 4) & 0x03
    if afc < 2: return None
    af_len = pkt[4]
    if af_len < 7: return None
    af_flags = pkt[5]
    if not (af_flags & 0x10): return None
    b0, b1, b2, b3, b4, e1 = struct.unpack(">BBBBBB", pkt[6:12])
    pcr_base = (b0 << 25) | (b1 << 17) | (b2 << 9) | (b3 << 1) | (b4 >> 7)
    pcr_ext = ((b4 & 0x01) << 8) | e1
    return pcr_base * 300 + pcr_ext

def analyze_pid_bitrate_pcr(ts_file, target_pid, pcr_pid, window_sec, skip_sec):
    packet_size = 188
    pcr_clock = 27000000

    first_pcr_val = None
    window_start_pcr = None
    current_window_target_bytes = 0

    windows = []

    print(f"[*] Analyzing Physical PID Bitrate in: {ts_file}")
    print(f"[*] Target PID: 0x{target_pid:04x}")
    print(f"[*] PCR Source PID: 0x{pcr_pid:04x}")
    print(f"[*] Window: {window_sec}s, Skip: {skip_sec}s")

    with open(ts_file, 'rb') as f:
        pos = 0
        while True:
            pkt = f.read(packet_size)
            if len(pkt) < packet_size:
                break

            if pkt[0] != 0x47:
                pos += 1
                f.seek(pos)
                continue

            pid = ((pkt[1] & 0x1f) << 8) | pkt[2]

            # 1. Update Clock if this packet has PCR
            if pid == pcr_pid:
                pcr = parse_pcr(pkt)
                if pcr is not None:
                    if first_pcr_val is None:
                        first_pcr_val = pcr

                    rel_time = (pcr - first_pcr_val) / pcr_clock

                    if rel_time >= skip_sec:
                        if window_start_pcr is None:
                            window_start_pcr = pcr
                            current_window_target_bytes = 0
                        else:
                            duration_sec = (pcr - window_start_pcr) / pcr_clock
                            if duration_sec >= window_sec:
                                bitrate_kbps = (current_window_target_bytes * 8) / (duration_sec * 1000.0)
                                windows.append((rel_time, bitrate_kbps))
                                window_start_pcr = pcr
                                current_window_target_bytes = 0

            # 2. Accumulate bytes for the target PID
            if pid == target_pid:
                # If we are in the steady state, count it
                if first_pcr_val is not None:
                    rel_time_approx = (last_known_pcr - first_pcr_val) / pcr_clock if 'last_known_pcr' in locals() else 0
                    if rel_time_approx >= skip_sec:
                        current_window_target_bytes += packet_size

            if pid == pcr_pid:
                pcr_val = parse_pcr(pkt)
                if pcr_val is not None:
                    last_known_pcr = pcr_val

            pos += packet_size

    if not windows:
        print("[FAIL] No valid windows analyzed. Check PID/PCR/Skip settings.")
        return

    bitrates = [w[1] for w in windows]
    mean_br = sum(bitrates) / len(bitrates)
    max_br = max(bitrates)
    min_br = min(bitrates)

    print("\n" + "="*45)
    print(f"  PID 0x{target_pid:04x} PHYSICAL BITRATE REPORT")
    print("="*45)
    print(f"Mean Bitrate:    {mean_br:12.2f} kbps")
    print(f"Max Bitrate:     {max_br:12.2f} kbps")
    print(f"Min Bitrate:     {min_br:12.2f} kbps")
    print(f"Fluctuation:     {(max_br - min_br):12.2f} kbps")
    print(f"Fluct Percent:   {((max_br - min_br)/mean_br*100):12.2f} %")
    print("="*45)

    print("\n--- Time Series (Steady State) ---")
    print("Time (s) | Bitrate (kbps)")
    print("---------|---------------")
    for t, br in windows:
        print(f"{t:8.2f} | {br:13.2f}")

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Physical PID Bitrate Analyzer (PCR-referenced)")
    parser.add_argument("file", help="Path to .ts file")
    parser.add_argument("--pid", default="0x0100", help="Target PID to measure (default: 0x0100)")
    parser.add_argument("--pcr", default="0x0100", help="PCR source PID (default: 0x0100)")
    parser.add_argument("--window", type=float, default=1.0, help="Window size in seconds")
    parser.add_argument("--skip", type=float, default=3.0, help="Seconds to skip from start")

    args = parser.parse_args()

    target_pid = int(args.pid, 16) if args.pid.startswith('0x') else int(args.pid)
    pcr_pid = int(args.pcr, 16) if args.pcr.startswith('0x') else int(args.pcr)

    analyze_pid_bitrate_pcr(args.file, target_pid, pcr_pid, args.window, args.skip)
