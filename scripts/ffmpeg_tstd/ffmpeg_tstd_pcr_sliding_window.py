#!/usr/bin/env python3
import sys
import argparse
from collections import deque

TS_PACKET_SIZE = 188
PCR_CLOCK = 27000000  # 27 MHz


def parse_ts_packet(pkt):
    if pkt[0] != 0x47:
        return None

    pid = ((pkt[1] & 0x1F) << 8) | pkt[2]
    afc = (pkt[3] >> 4) & 0x3

    has_adaptation = afc in (2, 3)

    pcr = None

    if has_adaptation:
        afl = pkt[4]
        if afl > 0:
            flags = pkt[5]
            if flags & 0x10:  # PCR flag
                pcr_base = (
                    (pkt[6] << 25)
                    | (pkt[7] << 17)
                    | (pkt[8] << 9)
                    | (pkt[9] << 1)
                    | (pkt[10] >> 7)
                )
                pcr_ext = ((pkt[10] & 1) << 8) | pkt[11]
                pcr = pcr_base * 300 + pcr_ext

    return pid, pcr


def run_pcr_mode(args):
    window_ticks = int(args.window_ms / 1000.0 * PCR_CLOCK)

    timeline = []
    cumulative_bits = 0

    with open(args.input, "rb") as f:
        while True:
            pkt = f.read(TS_PACKET_SIZE)
            if len(pkt) < TS_PACKET_SIZE:
                break

            parsed = parse_ts_packet(pkt)
            if not parsed:
                continue

            pid, pcr = parsed

            if pid == args.vid_pid:
                cumulative_bits += TS_PACKET_SIZE * 8

            if pid == args.pcr_pid and pcr is not None:
                timeline.append((pcr, cumulative_bits))

    if not timeline:
        print("No PCR found")
        return

    if args.skip_sec > 0:
        skip_ticks = int(args.skip_sec * PCR_CLOCK)
        base = timeline[0][0]
        timeline = [(p, b) for (p, b) in timeline if p >= base + skip_ticks]

    if len(timeline) < 2:
        print("Not enough PCR points after skip")
        return

    bitrates = []
    j = 0
    n = len(timeline)

    for i in range(n):
        pcr_i, bits_i = timeline[i]

        while j < n and timeline[j][0] - pcr_i < window_ticks:
            j += 1

        if j >= n:
            break

        pcr_j, bits_j = timeline[j]

        delta_bits = bits_j - bits_i
        delta_time = (pcr_j - pcr_i) / PCR_CLOCK

        if delta_time > 0:
            bitrate = delta_bits / delta_time
            bitrates.append(bitrate)

    return bitrates


def run_promax_mode(args):
    if args.muxrate <= 0:
        print("Error: --muxrate required for promax mode")
        sys.exit(1)

    window_sec = args.window_ms / 1000.0

    time = 0.0
    window = deque()
    window_bits = 0
    bitrates = []

    with open(args.input, "rb") as f:
        while True:
            pkt = f.read(TS_PACKET_SIZE)
            if len(pkt) < TS_PACKET_SIZE:
                break

            if pkt[0] != 0x47:
                continue

            pid = ((pkt[1] & 0x1F) << 8) | pkt[2]

            # 👉 TS pacing 时间轴（关键）
            time += TS_PACKET_SIZE * 8 / args.muxrate

            is_video = (pid == args.vid_pid)

            window.append((time, is_video))

            if is_video:
                window_bits += TS_PACKET_SIZE * 8

            # 滑动窗口
            while window and window[0][0] < time - window_sec:
                old_time, old_is_video = window.popleft()
                if old_is_video:
                    window_bits -= TS_PACKET_SIZE * 8

            if (
                time > args.skip_sec
                and window
                and (window[-1][0] - window[0][0]) >= window_sec * 0.99
            ):
                bitrate = window_bits / window_sec
                bitrates.append(bitrate)

    return bitrates


def print_stats(bitrates, window_ms, mode):
    if not bitrates:
        print("No valid bitrate samples")
        return

    mean_br = sum(bitrates) / len(bitrates)
    max_br = max(bitrates)
    min_br = min(bitrates)
    fluct = max_br - min_br

    print("\n==================================================")
    print(f"  {mode.upper()} BITRATE AUDIT")
    print("==================================================")
    print(f"  Window Time : {window_ms} ms")
    print(f"  Mean Bitrate: {mean_br:.2f} bps")
    print(f"  Max Bitrate : {max_br:.2f} bps")
    print(f"  Min Bitrate : {min_br:.2f} bps")
    print(f"  Fluctuation : {fluct:.2f} bps")
    print("==================================================")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("input")
    ap.add_argument("--pcr_pid", type=lambda x: int(x, 0), required=True)
    ap.add_argument("--vid_pid", type=lambda x: int(x, 0), required=True)
    ap.add_argument("--window_ms", type=float, default=1504.0)
    ap.add_argument("--skip_sec", type=float, default=0.0)

    # 🔥 新增
    ap.add_argument("--mode", choices=["pcr", "promax"], default="pcr")
    ap.add_argument("--muxrate", type=float, default=0)

    args = ap.parse_args()

    if args.mode == "pcr":
        bitrates = run_pcr_mode(args)
    else:
        bitrates = run_promax_mode(args)

    print_stats(bitrates, args.window_ms, args.mode)


if __name__ == "__main__":
    main()
