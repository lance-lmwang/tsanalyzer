import sys

def analyze(filename, target_bitrate_bps, ts_per_udp):
    # Constants
    UDP_PAYLOAD_SIZE = ts_per_udp * 188
    # Standard Ethernet/UDP/IP overhead is approx 42-46 bytes,
    # but on loopback it might differ. We use the payload for bitrate calculation.

    times = []
    with open(filename, 'r') as f:
        for line in f:
            try:
                # Expecting format from tcpdump -tt: 1677000000.123456 IP ...
                parts = line.split()
                if len(parts) > 0:
                    times.append(float(parts[0]))
            except ValueError:
                continue

    if len(times) < 2:
        print("Error: Not enough packets captured.")
        return

    gaps = [(times[i] - times[i-1]) * 1000000 for i in range(1, len(times))] # in microseconds

    avg_gap = sum(gaps) / len(gaps)
    max_gap = max(gaps)
    min_gap = min(gaps)
    std_dev = (sum((x - avg_gap)**2 for x in gaps) / len(gaps))**0.5

    # Bitrate calculation based on IPG
    # bps = (bits per packet) / (seconds per packet)
    calc_bitrate = (UDP_PAYLOAD_SIZE * 8) / (avg_gap / 1000000)

    print(f"=== CBR Analysis Results ===")
    print(f"Total Packets: {len(times)}")
    print(f"Target Bitrate: {target_bitrate_bps / 1000000:.2f} Mbps")
    print(f"Measured Bitrate: {calc_bitrate / 1000000:.2f} Mbps")
    print(f"--- Timing (Microseconds) ---")
    print(f"Theoretical IPG: {(UDP_PAYLOAD_SIZE * 8 / target_bitrate_bps) * 1000000:.2f} us")
    print(f"Average IPG: {avg_gap:.2f} us")
    print(f"Max IPG: {max_gap:.2f} us")
    print(f"Min IPG: {min_gap:.2f} us")
    print(f"Max Jitter (Pk-Pk): {max_gap - min_gap:.2f} us")
    print(f"Standard Deviation: {std_dev:.2f} us")

if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Usage: python3 analyze_ipg.py <capture_file>")
    else:
        analyze(sys.argv[1], 20000000, 7)
