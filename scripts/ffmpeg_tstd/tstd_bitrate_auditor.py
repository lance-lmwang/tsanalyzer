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
    # Regex captures STC and PID, but ONLY from lines representing actual packet emission.
    # This ensures we count physical bytes, not telemetry noise.
    pattern = re.compile(r"\[T-STD\]\s+STC:(\d+)\s+PID:(\d+).*ACT:(PES|PCR_ONLY|NULL|PSI|DATA)")
    data = []

    target_pid = int(args.pid, 0) if args.pid.startswith('0x') else int(args.pid)

    with open(args.log, 'r') as f:
        for line in f:
            match = pattern.search(line)
            if match:
                stc = int(match.group(1))
                pid = int(match.group(2))
                if pid == target_pid:
                    data.append(stc)

    if not data:
        print(f"[ERROR] No packet emission data found for PID {args.pid} in {args.log}")
        return

    # Create DataFrame
    df = pd.DataFrame(data, columns=['stc'])

    # Convert STC to relative seconds
    first_stc = df['stc'].min()
    df['time'] = (df['stc'] - first_stc) / 27000000.0

    # Group by window
    df['window'] = (df['time'] / args.window).astype(int)

    # Formula: (Count * 188 bytes * 8 bits) / (WindowSize * 1000) -> kbps
    # Note: We use the actual packet count within the STC-defined window.
    bitrate_series = df.groupby('window').size() * 188 * 8 / (args.window * 1000.0)

    # Mapping back to time for filtering
    bitrate_df = bitrate_series.reset_index(name='kbps')
    bitrate_df['time'] = bitrate_df['window'] * args.window

    # Filter for Steady State (Global Skip and Skip-Tail)
    max_time = bitrate_df['time'].max()
    steady = bitrate_df[(bitrate_df['time'] >= args.skip) & (bitrate_df['time'] <= (max_time - args.skip_tail))]

    if steady.empty:
        print("[WARN] Not enough steady-state data to perform audit. Check log duration.")
        return

    # Statistics calculation
    mean_br = steady['kbps'].mean()
    max_br = steady['kbps'].max()
    min_br = steady['kbps'].min()
    fluctuation = max_br - min_br
    fluct_percent = (fluctuation / mean_br * 100) if mean_br > 0 else 0
    std_dev = steady['kbps'].std()

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
        print(steady[['time', 'kbps']].to_string(index=False))

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
