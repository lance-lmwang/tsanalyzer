#!/usr/bin/env python3
"""
T-STD Bitrate Auditor Pro (STC-Physical Edition)
Professional Tool for PCR-relative Bitrate Analysis based on absolute STC timestamps.
"""

import re
import sys
import argparse
import pandas as pd
import numpy as np

def analyze_bitrate(args):
    # Regex for new Telemetry format: [T-STD SEC]   1s | In: 941k | Out: 601k ...
    pattern = re.compile(r"\[T-STD SEC\].*Out:\s*(\d+)k")
    data = []

    with open(args.log, 'r') as f:
        for line in f:
            match = pattern.search(line)
            if match:
                kbps = float(match.group(1))
                data.append(kbps)

    if not data:
        print(f"[ERROR] No packet emission data found in {args.log}. Did you use -mpegts_tstd_debug 1 or 2?")
        return

    # Create Series
    bitrate_series = pd.Series(data)

    # Filter for Steady State
    max_time = len(bitrate_series)
    start_idx = int(args.skip)
    end_idx = int(max_time - args.skip_tail)

    if end_idx <= start_idx:
        print("[WARN] Not enough steady-state data to perform audit.")
        return

    steady = bitrate_series[start_idx:end_idx]

    if steady.empty:
        print("[WARN] Not enough steady-state data to perform audit. Check log duration.")
        return

    # Statistics calculation
    mean_br = steady.mean()
    max_br = steady.max()
    min_br = steady.min()
    fluctuation = max_br - min_br
    fluct_percent = (fluctuation / mean_br * 100) if mean_br > 0 else 0
    std_dev = steady.std()

    print("\n=============================================")
    print(f"  BITRATE AUDIT SUMMARY (Window: {args.window}s)")
    print("=============================================")
    print(f"Mean Bitrate:      {mean_br:12.2f} kbps")
    print(f"Max Bitrate:       {max_br:12.2f} kbps")
    print(f"Min Bitrate:       {min_br:12.2f} kbps")
    print(f"Fluctuation:       {fluctuation:12.2f} kbps")
    print(f"Fluct Percent:     {fluct_percent:12.2f} %")
    print(f"Standard Dev:      {std_dev:12.2f} kbps")
    print("=============================================")

    if args.verbose:
        print("\n[Time Series Data]")
        for i, val in steady.items():
            print(f"Time: {i:4d}s | Rate: {val:10.2f} kbps")

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Professional T-STD Bitrate Auditor (STC-based)")
    parser.add_argument("--log", required=True, help="Path to FFmpeg trace/debug log")
    parser.add_argument("--pid", default="0x0100", help="Target PID (hex or dec, default: 0x0100)")
    parser.add_argument("--window", type=float, default=1.0, help="Analysis window size in seconds (default: 1.0)")
    parser.add_argument("--skip", type=float, default=3.0, help="Startup time to skip in seconds (default: 3.0)")
    parser.add_argument("--skip-tail", type=float, default=3.0, help="EOF time to skip in seconds (default: 3.0)")
    parser.add_argument("--verbose", action="store_true", help="Print full time-series table")

    args = parser.parse_args()
    analyze_bitrate(args)
