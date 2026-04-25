import sys
import os

def analyze_sdt_interval(ts_path, mux_rate_bps):
    PACKET_SIZE = 188
    SDT_PID = 0x0011
    TABLE_ID_SDT = 0x42

    last_sdt_packet_idx = -1
    max_interval_ms = 0
    intervals = []
    total_packets = 0

    print(f"[*] Analyzing SDT Compliance: {ts_path}")
    print(f"[*] Muxrate: {mux_rate_bps / 1000:.2f} kbps")

    if not os.path.exists(ts_path):
        print(f"[FAIL] File not found: {ts_path}")
        return

    with open(ts_path, 'rb') as f:
        while True:
            data = f.read(PACKET_SIZE)
            if not data or len(data) < PACKET_SIZE:
                break

            if data[0] != 0x47:
                # Search for next sync byte
                continue

            # Extract PID
            pid = ((data[1] & 0x1F) << 8) | data[2]
            pusi = (data[1] & 0x40) >> 6 # Payload Unit Start Indicator

            if pid == SDT_PID and pusi:
                # Check adaptation field
                afc = (data[3] & 0x30) >> 4
                payload_offset = 4
                if afc == 2 or afc == 3: # Has adaptation field
                    if payload_offset < len(data):
                        payload_offset += 1 + data[4]

                # Pointer field for PSI
                if payload_offset < PACKET_SIZE:
                    pointer_field = data[payload_offset]
                    section_start = payload_offset + 1 + pointer_field

                    if section_start < PACKET_SIZE and data[section_start] == TABLE_ID_SDT:
                        if last_sdt_packet_idx != -1:
                            bits_diff = (total_packets - last_sdt_packet_idx) * PACKET_SIZE * 8
                            interval_ms = (bits_diff * 1000.0) / mux_rate_bps
                            intervals.append(interval_ms)
                            if interval_ms > max_interval_ms:
                                max_interval_ms = interval_ms

                            if interval_ms > 2000:
                                print(f"[ERR] SDT VIOLATION: Interval {interval_ms:.2f}ms at packet {total_packets}")

                        last_sdt_packet_idx = total_packets

            total_packets += 1

    if not intervals:
        print("[FAIL] No SDT packets found!")
        return

    avg_interval = sum(intervals) / len(intervals)
    print("\n" + "="*40)
    print(f"  SDT REPETITION REPORT")
    print("="*40)
    print(f"  Total Packets    : {total_packets}")
    print(f"  SDT Count        : {len(intervals) + 1}")
    print(f"  Max Interval     : {max_interval_ms:.2f} ms {'[FAIL]' if max_interval_ms > 2000 else '[PASS]'}")
    print(f"  Min Interval     : {min(intervals):.2f} ms")
    print(f"  Avg Interval     : {avg_interval:.2f} ms")
    print("="*40)

if __name__ == "__main__":
    if len(sys.argv) < 3:
        print("Usage: python3 tstd_sdt_audit.py <file.ts> <mux_rate_bps>")
        sys.exit(1)

    analyze_sdt_interval(sys.argv[1], int(sys.argv[2]))
