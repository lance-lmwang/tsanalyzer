import sys
import os

def analyze_boundary_compliance(ts_path, mux_rate_bps):
    PACKET_SIZE = 188
    SDT_PID = 0x0011

    first_sdt_pkt = -1
    last_sdt_pkt = -1
    total_packets = 0

    if not os.path.exists(ts_path):
        return

    with open(ts_path, 'rb') as f:
        while True:
            data = f.read(PACKET_SIZE)
            if not data or len(data) < PACKET_SIZE:
                break

            pid = ((data[1] & 0x1F) << 8) | data[2]
            pusi = (data[1] & 0x40) >> 6

            if pid == SDT_PID and pusi:
                if first_sdt_pkt == -1:
                    first_sdt_pkt = total_packets
                last_sdt_pkt = total_packets

            total_packets += 1

    # Convert to time
    file_start_to_first_ms = (first_sdt_pkt * PACKET_SIZE * 8 * 1000.0) / mux_rate_bps
    last_to_file_end_ms = ((total_packets - last_sdt_pkt) * PACKET_SIZE * 8 * 1000.0) / mux_rate_bps

    print(f"[*] File: {ts_path}")
    print(f"[*] Start -> First SDT: {file_start_to_first_ms:.2f} ms")
    print(f"[*] Last SDT -> EOF      : {last_to_file_end_ms:.2f} ms")

    if file_start_to_first_ms > 2000 or last_to_file_end_ms > 2000:
        print("[FAIL] Boundary violation detected! (Must be < 2000ms)")
    else:
        print("[PASS] Boundary compliance OK.")

if __name__ == "__main__":
    analyze_boundary_compliance(sys.argv[1], int(sys.argv[2]))
