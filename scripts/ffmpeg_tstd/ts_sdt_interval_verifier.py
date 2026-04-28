import sys
import os

def analyze_tstd_compliance(ts_path):
    PACKET_SIZE = 188
    SDT_PID = 0x0011
    TABLE_ID_SDT = 0x42

    last_pcr = -1
    last_pcr_pkt_idx = -1

    current_stream_time_ms = 0

    last_sdt_time_ms = -1
    max_sdt_interval_ms = 0
    total_packets = 0
    violations = 0

    print(f"[*] Deep Auditing TS Timeline: {ts_path}")

    if not os.path.exists(ts_path):
        print(f"[FAIL] File not found: {ts_path}")
        return

    with open(ts_path, 'rb') as f:
        while True:
            data = f.read(PACKET_SIZE)
            if not data or len(data) < PACKET_SIZE:
                break

            if data[0] != 0x47:
                total_packets += 1
                continue

            # 1. Extract PCR for timeline tracking
            afc = (data[3] & 0x30) >> 4
            if (afc == 2 or afc == 3) and data[4] > 0: # Has AF
                has_pcr = data[5] & 0x10
                if has_pcr and data[4] >= 7:
                    pcr_base = (data[6] << 25) | (data[7] << 17) | (data[8] << 9) | (data[9] << 1) | (data[10] >> 7)
                    pcr_ext = ((data[10] & 0x01) << 8) | data[11]
                    pcr_val = pcr_base * 300 + pcr_ext

                    current_stream_time_ms = pcr_val / 27000.0

                    if last_pcr == -1:
                        print(f"[*] Stream Start PCR: {current_stream_time_ms:.2f} ms")

                    last_pcr = pcr_val
                    last_pcr_pkt_idx = total_packets

            # 2. Extract SDT
            pid = ((data[1] & 0x1F) << 8) | data[2]
            pusi = (data[1] & 0x40) >> 6

            if pid == SDT_PID and pusi:
                payload_offset = 4
                if afc == 2 or afc == 3:
                    payload_offset += 1 + data[4]

                if payload_offset < PACKET_SIZE:
                    pointer_field = data[payload_offset]
                    section_start = payload_offset + 1 + pointer_field

                    if section_start < PACKET_SIZE and data[section_start] == TABLE_ID_SDT:
                        # We found an SDT. Estimate its exact time based on last PCR
                        # If no PCR yet, use 0 or initial offset
                        sdt_time_ms = current_stream_time_ms

                        if last_sdt_time_ms != -1:
                            interval = sdt_time_ms - last_sdt_time_ms
                            if interval > 2000:
                                print(f"[ERR] SDT INTERVAL VIOLATION (Timeline): {interval:.2f} ms at packet {total_packets}")
                                violations += 1
                            if interval > max_sdt_interval_ms:
                                max_sdt_interval_ms = interval

                        last_sdt_time_ms = sdt_time_ms

            total_packets += 1

    print("\n" + "="*45)
    print(f"  TIMELINE-BASED SDT REPORT")
    print("="*45)
    print(f"  Max SDT Interval : {max_sdt_interval_ms:.2f} ms")
    print(f"  Total Violations : {violations}")
    print(f"  Status           : {'[FAIL]' if violations > 0 else '[PASS]'}")
    print("="*45)

if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Usage: python3 ts_sdt_interval_verifier.py <file.ts>")
        sys.exit(1)
    analyze_tstd_compliance(sys.argv[1])
