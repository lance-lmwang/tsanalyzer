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
            return json.loads(r.read().decode())
    except Exception as e:
        return None

def main():
    parser = argparse.ArgumentParser(description='TsAnalyzer Real-time Monitor')
    parser.add_argument('--url', default='http://localhost:12345/api/v1/snapshot', help='API URL')
    parser.add_argument('--interval', type=float, default=1.0, help='Update interval in seconds')
    parser.add_argument('--duration', type=int, default=0, help='Total duration to run (0 for infinite)')
    args = parser.parse_args()

    start_time = time.time()
    try:
        while True:
            snap = get_snapshot(args.url)
            if snap:
                # ANSI Clear Screen
                print('\033[H\033[J', end='')
                ts = time.strftime('%H:%M:%S')
                link = snap.get('tier1_link', {})
                p1 = snap.get('tier2_compliance', {}).get('p1', {})
                p2 = snap.get('tier2_compliance', {}).get('p2', {})
                
                print(f'TsAnalyzer Live Monitor | Time: {ts} | Source: {snap.get("status", {}).get("input_label", "Unknown")}')
                print('='*80)
                print(f'Total Bitrate: {link.get("physical_bitrate_bps", 0)/1e6:>6.2f} Mbps | MDI-DF: {link.get("mdi_df_ms", 0):>6.2f} ms')
                print(f'CC Errors: {p1.get("cc_error", 0):>10} | PCR Jitter: {p2.get("pcr_jitter_ms", 0):>8.3f} ms')
                print('-'*80)
                print(f'{"PID":<8} | {"TYPE":<10} | {"BITRATE":<12} | {"EB FILL":<10}')
                print('-'*80)
                
                pids = snap.get('pids', [])
                pids.sort(key=lambda x: x.get('bitrate_bps', 0), reverse=True)
                for p in pids[:12]:
                    pid = p['pid']
                    ptype = p['type']
                    pbr = p.get('bitrate_bps', 0) / 1e6
                    peb = p.get('eb_fill_pct', 0)
                    print(f'{pid:<8} | {ptype:<10} | {pbr:>9.2f} Mbps | {peb:>8.2f}%')
            else:
                print(f'[{time.strftime("%H:%M:%S")}] Waiting for TsAnalyzer engine at {args.url}...', end='')
            
            if args.duration > 0 and (time.time() - start_time) > args.duration:
                break
            time.sleep(args.interval)
    except KeyboardInterrupt:
        print('
Monitor stopped.')

if __name__ == '__main__':
    main()
