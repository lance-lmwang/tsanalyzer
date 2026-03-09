#!/usr/bin/env python3
import sys
import os
import argparse
from math import sqrt

def parse_pcr(pkt):
    if len(pkt) < 188: return None
    afc = (pkt[3] & 0x30) >> 4
    if afc < 2: return None 
    af_len = pkt[4]
    if af_len < 7 or not (pkt[5] & 0x10): return None
    
    base = (pkt[6] << 25) | (pkt[7] << 17) | (pkt[8] << 9) | (pkt[9] << 1) | (pkt[10] >> 7)
    ext = ((pkt[10] & 0x01) << 8) | pkt[11]
    return base * 300 + ext

class PCRStats:
    def __init__(self, pid):
        self.pid = pid
        self.last_pcr = None
        self.last_offset = None
        self.bitrates = []
        self.intervals_ms = []
        self.pcr_count = 0

    def add_sample(self, pcr, offset):
        if self.last_pcr is not None:
            # ISO/IEC 13818-1 Wrap-around logic
            pcr_modulo = 2576980377600
            delta_pcr = (pcr - self.last_pcr) % pcr_modulo
            delta_bytes = offset - self.last_offset
            
            if delta_pcr > 0:
                bitrate = (delta_bytes * 8 * 27000000) / delta_pcr
                self.bitrates.append(bitrate)
                self.intervals_ms.append(delta_pcr / 27000.0)
        
        self.last_pcr = pcr
        self.last_offset = offset
        self.pcr_count += 1

def main():
    parser = argparse.ArgumentParser(description='Industrial PCR & Bitrate Analyzer (ISO/IEC 13818-1)')
    parser.add_argument('file', help='Input TS file')
    parser.add_argument('--pid', type=lambda x: int(x, 0), help='Filter by specific PID (e.g. 0x100)')
    parser.add_argument('--limit', type=int, default=0, help='Limit number of PCR samples to analyze')
    parser.add_argument('--verbose', action='store_true', help='Show per-sample details')
    args = parser.parse_args()

    if not os.path.exists(args.file):
        print(f"Error: {args.file} not found")
        return

    tracks = {}
    with open(args.file, 'rb') as f:
        # 1. Initial Sync
        while True:
            pos = f.tell()
            b = f.read(1)
            if not b: break
            if b[0] == 0x47:
                f.seek(pos)
                break

        pkt_idx = 0
        while True:
            current_pos = f.tell()
            pkt = f.read(188)
            if len(pkt) < 188: break
            
            if pkt[0] != 0x47:
                print(f"Sync loss at byte {current_pos}, re-syncing...")
                f.seek(current_pos + 1)
                continue

            pid = ((pkt[1] & 0x1F) << 8) | pkt[2]
            if args.pid is None or pid == args.pid:
                pcr = parse_pcr(pkt)
                if pcr is not None:
                    if pid not in tracks: tracks[pid] = PCRStats(pid)
                    track = tracks[pid]
                    
                    # ISO Definition: Byte offset i is the index of the byte 
                    # containing the last bit of the PCR-base.
                    # We use the end of the packet for simplicity in byte-delta.
                    track.add_sample(pcr, current_pos + 188)
                    
                    if args.verbose:
                        if len(track.bitrates) > 0:
                            print(f"PID: 0x{pid:04x} | Offset: {current_pos:12} | "
                                  f"Interval: {track.intervals_ms[-1]:6.2f}ms | "
                                  f"Rate: {track.bitrates[-1]/1e6:8.4f} Mbps")

                    if args.limit > 0 and track.pcr_count >= args.limit:
                        break
            pkt_idx += 1

    print("\n" + "="*80)
    print(f"{'PID':<10} | {'Samples':<8} | {'Avg Rate':<12} | {'Max Gap':<10} | {'Stability'}")
    print("-" * 80)
    
    for pid in sorted(tracks.keys()):
        t = tracks[pid]
        if not t.bitrates: continue
        
        avg_rate = sum(t.bitrates) / len(t.bitrates)
        max_gap = max(t.intervals_ms)
        
        # Calculate standard deviation for stability
        variance = sum((x - avg_rate)**2 for x in t.bitrates) / len(t.bitrates)
        std_dev = sqrt(variance)
        stability = "EXCELLENT" if (std_dev / avg_rate) < 0.001 else "STABLE" if (std_dev / avg_rate) < 0.01 else "UNSTABLE"
        
        print(f"0x{pid:04x}     | {t.pcr_count:<8} | {avg_rate/1e6:8.4f}M | {max_gap:6.2f} ms | {stability} (σ={std_dev/1e3:.2f}k)")

    print("="*80)

if __name__ == "__main__":
    main()
