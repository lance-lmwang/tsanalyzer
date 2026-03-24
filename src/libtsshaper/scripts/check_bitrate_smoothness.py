import sys
import os

def analyze_bitrate(file_path, pid=0x100, target_bps=10000000):
    packet_size = 188
    file_size = os.path.getsize(file_path)

    # 100ms window at 10Mbps is 1,000,000 bits = 125,000 bytes = ~664 packets
    window_ms = 100
    packets_per_window = int((target_bps / 8) * (window_ms / 1000.0) / packet_size)

    print(f"Analyzing {file_path}...")
    print(f"Target Bitrate: {target_bps/1e6:.2f} Mbps")
    print(f"Window Size: {window_ms} ms (~{packets_per_window} packets)")

    with open(file_path, 'rb') as f:
        data = f.read()

    total_packets = len(data) // packet_size
    window_bitrates = []

    for i in range(0, total_packets - packets_per_window, packets_per_window):
        window_data = data[i*packet_size : (i+packets_per_window)*packet_size]
        video_packets = 0
        for j in range(packets_per_window):
            p = window_data[j*packet_size : (j+1)*packet_size]
            if len(p) < 4: continue
            current_pid = ((p[1] & 0x1F) << 8) | p[2]
            if current_pid == pid:
                video_packets += 1

        # Calculate instantaneous bitrate for this window
        # (packets * 188 * 8) / (window_sec)
        bits = video_packets * packet_size * 8
        bps = bits / (window_ms / 1000.0)
        window_bitrates.append(bps)

    if not window_bitrates:
        print("No data found.")
        return

    avg_bps = sum(window_bitrates) / len(window_bitrates)
    max_bps = max(window_bitrates)
    min_bps = min(window_bitrates)

    # Calculate Standard Deviation
    variance = sum((x - avg_bps) ** 2 for x in window_bitrates) / len(window_bitrates)
    std_dev = variance ** 0.5

    print("\nResults:")
    print(f"- Measured Avg Bitrate: {avg_bps/1e6:.4f} Mbps")
    print(f"- Max Window Bitrate:   {max_bps/1e6:.4f} Mbps")
    print(f"- Min Window Bitrate:   {min_bps/1e6:.4f} Mbps")
    print(f"- Standard Deviation:   {std_dev/1e6:.4f} Mbps")
    print(f"- Fluctuation Ratio:    {(max_bps - min_bps)/avg_bps*100:.2f}%")

    # If std dev is less than 5% of target, we consider it smooth
    if std_dev < (target_bps * 0.05):
        print("\n[PASS] Bitrate is extremely smooth.")
    else:
        print("\n[FAIL] Bitrate fluctuation is too high.")

if __name__ == "__main__":
    import argparse
    parser = argparse.ArgumentParser(description='Analyze TS bitrate smoothness')
    parser.add_argument('file', help='TS file to analyze')
    parser.add_argument('--pid', type=lambda x: int(x, 0), default=0x100, help='PID to analyze (default: 0x100)')
    parser.add_argument('--target', type=int, default=10000000, help='Target bitrate in bps (default: 10Mbps)')

    args = parser.parse_args()
    analyze_bitrate(args.file, pid=args.pid, target_bps=args.target)
