#!/usr/bin/env python3
import sys
import argparse
from collections import deque

TS_PACKET_SIZE = 188

def parse_args():
    parser = argparse.ArgumentParser(description="PROMAX-style Physical TS Packet Analyzer")
    parser.add_argument("ts_file", help="Path to the MPEG-TS file")
    parser.add_argument("--pid", type=lambda x: int(x, 0), required=True, help="Target PID in hex (e.g., 0x21)")
    parser.add_argument("--muxrate", type=int, default=1100000, help="Total TS muxrate in bps (default: 1100k)")
    parser.add_argument("--window_ms", type=float, default=1504.0, help="Sliding window time in ms (default: 1504)")
    parser.add_argument("--skip_sec", type=float, default=5.0, help="Seconds to skip at the beginning to allow stabilization")
    return parser.parse_args()

def main():
    args = parse_args()

    # 计算滑动窗口对应的物理包数量
    # 窗口容量(bits) = muxrate(bps) * (window_ms / 1000.0)
    # 窗口包数 = 窗口容量(bits) / (188 * 8)
    window_bits = args.muxrate * (args.window_ms / 1000.0)
    window_packets = int(window_bits / (TS_PACKET_SIZE * 8))

    # 跳过的起始包数
    skip_bits = args.muxrate * args.skip_sec
    skip_packets = int(skip_bits / (TS_PACKET_SIZE * 8))

    print(f"[*] PROMAX Audit Engine Initialized:")
    print(f"    - Target PID: 0x{args.pid:04X} ({args.pid})")
    print(f"    - Muxrate: {args.muxrate} bps")
    print(f"    - Window: {args.window_ms} ms -> exactly {window_packets} TS packets")
    print(f"    - Skipping first {args.skip_sec}s ({skip_packets} packets)")

    window = deque(maxlen=window_packets)
    target_count = 0
    total_packets = 0

    max_br = 0.0
    min_br = float('inf')
    sum_br = 0.0
    audit_points = 0

    try:
        with open(args.ts_file, 'rb') as f:
            while True:
                packet = f.read(TS_PACKET_SIZE)
                if not packet or len(packet) < TS_PACKET_SIZE:
                    break

                # 同步字节检查
                if packet[0] != 0x47:
                    # 尝试寻找下一个同步字节 (简单容错)
                    continue

                total_packets += 1

                # 解析 PID (包头第2、3字节，去掉前3个标志位)
                pid = ((packet[1] & 0x1F) << 8) | packet[2]

                is_target = 1 if pid == args.pid else 0

                # 维护滑动窗口
                if len(window) == window_packets:
                    # 移除最老的包
                    old_is_target = window.popleft()
                    target_count -= old_is_target

                window.append(is_target)
                target_count += is_target

                # 只有在跳过初始阶段，且窗口填满后才开始统计
                if total_packets > skip_packets and len(window) == window_packets:
                    # 瞬时码率 = (窗口内目标包数量 * 每个包的比特数) / 物理时间窗(秒)
                    # 等价于: (target_count * 188 * 8) / (window_ms / 1000)
                    current_bps = (target_count * TS_PACKET_SIZE * 8) / (args.window_ms / 1000.0)

                    if current_bps > max_br: max_br = current_bps
                    if current_bps < min_br: min_br = current_bps
                    sum_br += current_bps
                    audit_points += 1

    except FileNotFoundError:
        print(f"[!] Error: File {args.ts_file} not found.")
        sys.exit(1)

    if audit_points > 0:
        mean_br = sum_br / audit_points
        fluct = max_br - min_br
        print("\n=============================================")
        print("  PROMAX ALIGNED BITRATE AUDIT (Physical)")
        print("=============================================")
        print(f"  Window Time : {args.window_ms} ms")
        print(f"  Mean Bitrate: {mean_br:10.2f} bps")
        print(f"  Max Bitrate : {max_br:10.2f} bps")
        print(f"  Min Bitrate : {min_br:10.2f} bps")
        print(f"  Fluctuation : {fluct:10.2f} bps")
        print("=============================================")
    else:
        print("[!] Not enough data to audit after skipping.")

if __name__ == "__main__":
    main()
