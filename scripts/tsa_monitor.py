#!/usr/bin/env python3
import urllib.request
import json
import time
import os
import sys
import argparse

def get_snapshot(url):
    try:
        with urllib.request.urlopen(url, timeout=1) as r:
            data = json.loads(r.read().decode())
            if isinstance(data, list):
                return data
            return [data]
    except: return None

def format_row(label, value, unit="", color=""):
    if isinstance(value, float):
        return f"{label:<25} : {value:>10.2f} {unit:<5}"
    return f"{label:<25} : {value:>10} {unit:<5}"

def display_dashboard(snap):
    # Extract Tiers
    status = snap.get('status', {})
    link = snap.get('tier1_link', {})
    p1 = snap.get('tier2_compliance', {}).get('p1', {})
    p2 = snap.get('tier2_compliance', {}).get('p2', {})
    essence = snap.get('tier3_essence', {})
    pred = snap.get('tier4_predictive', {})
    pids = snap.get('pids', [])

    print(f"┌──────────────────────────────────────────────────────────────────────────────┐")
    print(f"│ TsAnalyzer Pro NOC - 7-Tier Metrology Dashboard        {time.strftime('%H:%M:%S')}      │")
    print(f"├──────────────────────────────────────────────────────────────────────────────┤")

    # T1: Master
    fid = status.get('master_health', 0)
    lock = "LOCKED" if status.get('signal_lock') else "LOSS"
    label = status.get('input_label', 'Unknown')
    print(f"│ SOURCE: {label:<15} | LOCK: {lock:<10} | FIDELITY: {fid:>5.1f}% │")
    print(f"├──────────────────────────────────────────────────────────────────────────────┤")

    # T2: Link
    print(f"│ [T2] TRANSPORT & LINK                                                        │")
    print(f"│ " + format_row("Physical Bitrate", link.get('physical_bitrate_bps',0)/1e6, "Mbps") + " | " + format_row("MDI Delay Factor", link.get('mdi_df_ms',0), "ms") + " │")
    print(f"│ " + format_row("SRT RTT", link.get('srt_rtt_ms',0), "ms") + " | " + format_row("SRT Retransmit", link.get('srt_retransmit_pct',0), "%") + "  │")
    print(f"├──────────────────────────────────────────────────────────────────────────────┤")

    # T3 & T4: Compliance
    print(f"│ [T3] ETR 290 P1 (CRITICAL)        │ [T4] ETR 290 P2 (TIMING)                 │")
    print(f"│ " + f"CC Errors: {p1.get('cc_error',0):<14}" + " │ " + f"PCR Jitter: {p2.get('pcr_jitter_ms',0):>8.3f} ms" + "          │")
    print(f"│ " + f"PAT Errors: {p1.get('pat_error',0):<13}" + " │ " + f"PCR Repet:  {p2.get('pcr_repetition',0):>8} ms" + "          │")
    print(f"├──────────────────────────────────────────────────────────────────────────────┤")

    # T5 & T6: Payload & Essence
    print(f"│ [T5] MUX PAYLOAD DYNAMICS         │ [T6] ESSENCE QUALITY                     │")
    print(f"│ " + f"Active PIDs: {len(pids):<14}" + " │ " + f"Video FPS: {essence.get('video_fps',0):>8.2f}" + "             │")
    print(f"│ " + f"Null Density: {0:<13}" + " │ " + f"AV Sync:   {essence.get('av_sync_offset_ms',0):>8} ms" + "          │")
    print(f"├──────────────────────────────────────────────────────────────────────────────┤")

    # T7: Predictive/Forensic
    rst = pred.get('rst_encoder_s', 0)
    print(f"│ [T7] PREDICTIVE: Remaining Safe Time (RST): {rst:>6.2f} s                       │")
    print(f"└──────────────────────────────────────────────────────────────────────────────┘")

    print(f"\nTop PIDs:")
    print(f"{'PID':<8} | {'TYPE':<10} | {'BITRATE':<12} | {'EB FILL':<8}")
    pids.sort(key=lambda x: x.get('bitrate_bps', 0), reverse=True)
    for p in pids[:5]:
        print(f"{p['pid']:<8} | {p['type']:<10} | {p.get('bitrate_bps',0)/1e6:>9.2f} M | {p.get('eb_fill_pct',0):>6.2f}%")

def main():
    parser = argparse.ArgumentParser(description='TsAnalyzer Professional NOC CLI')
    parser.add_argument('--url', default='http://localhost:12345/api/v1/snapshot', help='API URL')
    parser.add_argument('--duration', type=int, default=0, help='Duration in seconds')
    parser.add_argument('--verify', action='store_true', help='Verification mode for CI')
    args = parser.parse_args()

    start_time = time.time()
    stream_idx = 0
    try:
        while True:
            snaps = get_snapshot(args.url)
            if snaps:
                if args.verify:
                    # Verification mode: check the first stream for lock
                    snap = snaps[0]
                    status = snap.get('status', {})
                    p1 = snap.get('tier2_compliance', {}).get('p1', {})
                    if status.get('signal_lock') and p1.get('cc_error', 999) < 500:
                        print("VERIFY: PASS")
                        sys.exit(0)
                    elif (time.time() - start_time) > 15:
                        print("VERIFY: FAIL")
                        sys.exit(1)
                else:
                    print('\033[H\033[J', end='') # Clear Screen
                    if len(snaps) > 1:
                        print(f"Displaying Stream {stream_idx+1}/{len(snaps)} (Press Ctrl+C to stop)")
                    display_dashboard(snaps[stream_idx])
                    if len(snaps) > 1:
                        stream_idx = (stream_idx + 1) % len(snaps)
            else:
                print(f'\r[{time.strftime("%H:%M:%S")}] Waiting for TsAnalyzer engine at {args.url}...', end='', flush=True)

            if args.duration > 0 and (time.time() - start_time) > args.duration:
                break
            time.sleep(1)
    except KeyboardInterrupt:
        print("\nMonitor stopped.")

if __name__ == '__main__':
    main()
