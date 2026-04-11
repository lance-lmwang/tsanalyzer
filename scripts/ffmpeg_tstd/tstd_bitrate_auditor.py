#!/usr/bin/env python3
"""
T-STD Bitrate Auditor Pro
Professional Tool for PCR-relative Bitrate Analysis and Compliance Verification.
"""

import re
import sys
import argparse
import pandas as pd
import numpy as np

def analyze_bitrate(args):
    # Regex to capture STC and PID from T-STD telemetry
    pattern = re.compile(r"\[T-STD\]\s+STC:(\d+)\s+PID:(\d+)")
    data = []

    # Support both hex (0x100) and decimal (256) PID input
    try:
        if args.pid.startswith('0x'):
            target_pid = int(args.pid, 16)
        else:
            target_pid = int(args.pid)
    except ValueError:
        print(f"[ERROR] Invalid PID format: {args.pid}")
        sys.exit(1)

    print(f"[*] Auditing Bitrate for PID: {args.pid} ({target_pid})")
    print(f"[*] Log File: {args.log}")
    print(f"[*] Window Size: {args.window}s")
    print(f"[*] Startup Skip: {args.skip}s")

    try:
        with open(args.log, 'r') as f:
            for line in f:
                match = pattern.search(line)
                if match:
                    stc, pid = int(match.group(1)), int(match.group(2))
                    if pid == target_pid:
                        data.append(stc)
    except FileNotFoundError:
        print(f"[ERROR] Log file not found: {args.log}")
        sys.exit(1)

    if not data:
        print(f"[FAIL] No telemetry found for PID {args.pid} in the log.")
        return

    df = pd.DataFrame(data, columns=['stc'])
    # Convert 27MHz ticks to absolute seconds
    df['time_sec'] = df['stc'] / 27000000.0

    # Calculate grouping windows
    df['window'] = (df['time_sec'] / args.window).astype(int)

    # Calculate Bitrate in kbps (TS Layer: 188 bytes per packet)
    # Formula: (Count * 188 bytes * 8 bits) / (WindowSize * 1000)
    bitrate_series = df.groupby('window').size() * 188 * 8 / (args.window * 1000.0)

    # Mapping back to time for analysis
    bitrate_df = bitrate_series.reset_index(name='kbps')
    bitrate_df['time'] = bitrate_df['window'] * args.window

    # Filter for Steady State
    steady = bitrate_df[(bitrate_df['time'] >= args.skip)]
    # Drop the very last partial window if it exists
    if len(steady) > 1:
        steady = steady.iloc[:-1]

    if steady.empty:
        print("[WARN] Not enough data after skip period to perform audit.")
        return

    # Statistics calculation
    mean_val = steady['kbps'].mean()
    max_val = steady['kbps'].max()
    min_val = steady['kbps'].min()
    fluct_kbps = max_val - min_val
    fluct_pct = (fluct_kbps / mean_val * 100) if mean_val > 0 else 0
    std_val = steady['kbps'].std()

    print("\n" + "="*45)
    print(f"  BITRATE AUDIT SUMMARY (Window: {args.window}s)")
    print("="*45)
    print(f"Mean Bitrate:    {mean_val:12.2f} kbps")
    print(f"Max Bitrate:     {max_val:12.2f} kbps")
    print(f"Min Bitrate:     {min_val:12.2f} kbps")
    print(f"Fluctuation:     {fluct_kbps:12.2f} kbps")
    print(f"Fluct Percent:   {fluct_pct:12.2f} %")
    print(f"Standard Dev:    {std_val:12.2f} kbps")
    print("="*45)

    if args.verbose:
        print("\n--- Windowed Time Series ---")
        print("Time (s) | Bitrate (kbps)")
        print("---------|---------------")
        for _, row in steady.iterrows():
            print(f"{row['time']:8.1f} | {row['kbps']:15.2f}")

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Professional T-STD Bitrate Auditor")
    parser.add_argument("--log", required=True, help="Path to FFmpeg trace/debug log")
    parser.add_argument("--pid", default="0x0100", help="Target PID (hex or dec, default: 0x0100)")
    parser.add_argument("--window", type=float, default=1.0, help="Analysis window size in seconds (default: 1.0)")
    parser.add_argument("--skip", type=float, default=3.0, help="Startup time to skip in seconds (default: 3.0)")
    parser.add_argument("--verbose", action="store_true", help="Print full time-series table")

    args = parser.parse_args()
    analyze_bitrate(args)
