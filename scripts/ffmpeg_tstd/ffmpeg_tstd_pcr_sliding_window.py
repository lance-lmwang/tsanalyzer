#!/usr/bin/env python3
import sys
import argparse
from collections import deque

TS_PACKET_SIZE = 188
WINDOW_SEC = 1.0
SLICE_SEC = 0.1

def run_promax_mode(args):
    slices = deque([0] * int(WINDOW_SEC / SLICE_SEC))
    bitrates = []
    current_slice_bits = 0
    slice_timer = 0.0

    with open(args.input, "rb") as f:
        while True:
            pkt = f.read(TS_PACKET_SIZE)
            if len(pkt) < TS_PACKET_SIZE: break
            if pkt[0] != 0x47: continue
            pid = ((pkt[1] & 0x1F) << 8) | pkt[2]
            if pid == args.vid_pid:
                current_slice_bits += TS_PACKET_SIZE * 8

            if args.muxrate > 0:
                slice_timer += (TS_PACKET_SIZE * 8) / args.muxrate

            if slice_timer >= SLICE_SEC:
                slices.append(current_slice_bits)
                slices.popleft()
                current_slice_bits = 0
                slice_timer -= SLICE_SEC
                bitrates.append(sum(slices) / WINDOW_SEC)
    return bitrates

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("input")
    ap.add_argument("--vid_pid", type=lambda x: int(x, 0), required=True)
    ap.add_argument("--muxrate", type=float, required=True)
    args = ap.parse_args()

    bitrates = run_promax_mode(args)
    if not bitrates:
        print("0.00 0.00 0.00 0.00")
        sys.exit(0)

    mean = sum(bitrates) / len(bitrates)
    mx = max(bitrates)
    mn = min(bitrates)
    dev = max((mx - mean), (mean - mn)) / 1000
    print(f"{mean/1000:.2f} {mx/1000:.2f} {min(bitrates)/1000:.2f} {dev:.2f}")

if __name__ == "__main__":
    main()
