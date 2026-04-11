#!/usr/bin/env python3
import sys
import struct
import argparse

def parse_pcr(pkt):
    """Extract 27MHz PCR value from a 188-byte TS packet."""
    # Check adaptation field control (bits 4-5 of byte 3)
    afc = (pkt[3] >> 4) & 0x03
    if afc < 2: # No adaptation field
        return None

    af_len = pkt[4]
    if af_len < 7: # AF too short to contain PCR
        return None

    af_flags = pkt[5]
    if not (af_flags & 0x10): # PCR_flag not set
        return None

    # PCR base (33 bits) + reserved (6 bits) + PCR extension (9 bits)
    # We simplify to 27MHz ticks
    b0, b1, b2, b3, b4, e1 = struct.unpack(">BBBBBB", pkt[6:12])
    pcr_base = (b0 << 25) | (b1 << 17) | (b2 << 9) | (b3 << 1) | (b4 >> 7)
    pcr_ext = ((b4 & 0x01) << 8) | e1
    return pcr_base * 300 + pcr_ext

def analyze_ts_bitrate(ts_file, target_pid, window_sec, skip_sec):
    packet_size = 188
    pcr_clock = 27000000

    last_pcr = None
    last_pcr_pos = 0
    first_pcr_val = None

    windows = [] # (time_sec, bitrate_kbps)

    # We use a 1s rolling accumulator for the window
    current_window_bytes = 0
    window_start_pcr = None

    print(f"[*] Analyzing physical TS file: {ts_file}")
    print(f"[*] Target PCR PID: 0x{target_pid:04x}")
    print(f"[*] Skip initial: {skip_sec}s")

    with open(ts_file, 'rb') as f:
        pos = 0
        while True:
            pkt = f.read(packet_size)
            if len(pkt) < packet_size:
                break

            if pkt[0] != 0x47:
                # Attempt to resync if needed, but for clean files 0x47 is steady
                pos += 1
                f.seek(pos)
                continue

            pid = ((pkt[1] & 0x1f) << 8) | pkt[2]

            if pid == target_pid:
                pcr = parse_pcr(pkt)
                if pcr is not None:
                    if first_pcr_val is None:
                        first_pcr_val = pcr

                    rel_time = (pcr - first_pcr_val) / pcr_clock

                    if rel_time < skip_sec:
                        # Still in skip period, just update anchors
                        window_start_pcr = pcr
                        current_window_bytes = 0
                    else:
                        # Accumulate bytes
                        current_window_bytes += (pos - last_pcr_pos)

                        if window_start_pcr is None:
                            window_start_pcr = pcr
                            current_window_bytes = 0
                        else:
                            duration_sec = (pcr - window_start_pcr) / pcr_clock
                            if duration_sec >= window_sec:
                                # Calculate bitrate for this window
                                bitrate_kbps = (current_window_bytes * 8) / (duration_sec * 1000.0)
                                windows.append((rel_time, bitrate_kbps))

                                # Reset window
                                window_start_pcr = pcr
                                current_window_bytes = 0

                    last_pcr = pcr
                    last_pcr_pos = pos

            pos += packet_size

    if not windows:
        print("[FAIL] No PCR data found or duration too short.")
        return

    # Statistics
    bitrates = [w[1] for w in windows]
    mean_br = sum(bitrates) / len(bitrates)
    max_br = max(bitrates)
    min_br = min(bitrates)

    print("\n" + "="*45)
    print(f"  PHYSICAL TS BITRATE REPORT (Window: {window_sec}s)")
    print("="*45)
    print(f"Mean Bitrate:    {mean_br:12.2f} kbps")
    print(f"Max Bitrate:     {max_br:12.2f} kbps")
    print(f"Min Bitrate:     {min_br:12.2f} kbps")
    print(f"Fluctuation:     {(max_br - min_br):12.2f} kbps")
    print("="*45)

    print("\n--- Time Series (Steady State) ---")
    print("Time (s) | Bitrate (kbps)")
    print("---------|---------------")
    for t, br in windows:
        print(f"{t:8.2f} | {br:13.2f}")

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Physical TS Bitrate Analyzer (PCR-based)")
    parser.add_argument("file", help="Path to .ts file")
    parser.add_argument("--pid", default="0x0100", help="PID containing PCR (default: 0x0100)")
    parser.add_argument("--window", type=float, default=1.0, help="Window size in seconds")
    parser.add_argument("--skip", type=float, default=3.0, help="Seconds to skip from start")

    args = parser.parse_args()

    pid = int(args.pid, 16) if args.pid.startswith('0x') else int(args.pid)
    analyze_ts_bitrate(args.file, pid, args.window, args.skip)
