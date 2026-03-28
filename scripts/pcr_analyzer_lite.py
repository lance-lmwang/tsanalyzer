#!/usr/bin/env python3
"""
PCR Analyzer Lite (Zero-Dependency Version)
Supports both raw TS files and PCAPNG captures.
Validates PCR jitter and repetition intervals.
"""

import sys
import struct
import argparse
from decimal import Decimal

# =============================================================================
# Constants
# =============================================================================
PCAPNG_SHB = 0x0A0D0D0A
PCAPNG_IDB = 0x00000001
PCAPNG_EPB = 0x00000006
TS_PACKET_SIZE = 188
SYNC_BYTE = 0x47

# Thresholds
MAX_JITTER_PCAP_NS = 30    # Strict for network captures
MAX_JITTER_FILE_NS = 500   # Loose for raw TS files (local jitter)
MAX_INTERVAL_NS = 40_000_000

def extract_pcr_ns(ts_pkt):
    if len(ts_pkt) < 12 or ts_pkt[0] != SYNC_BYTE: return None
    afc = (ts_pkt[3] >> 4) & 0x03
    if afc < 2: return None
    if ts_pkt[4] < 7 or not (ts_pkt[5] & 0x10): return None

    # Unpack 6-byte PCR
    pcr_h, pcr_l = struct.unpack(">IH", ts_pkt[6:12])
    pcr_base = (pcr_h << 1) | (pcr_l >> 15)
    pcr_ext = pcr_l & 0x1FF
    return int(((pcr_base * 300) + pcr_ext) * 1000 // 27)

def analyze_raw_ts(file_path, target_pid):
    print(f"[*] Lite TS Analysis: {file_path} (PID 0x{target_pid:04X})")
    pcr_data = []
    pkt_count = 0

    with open(file_path, 'rb') as f:
        while True:
            pkt = f.read(TS_PACKET_SIZE)
            if not pkt: break
            if pkt[0] != SYNC_BYTE:
                f.seek(-TS_PACKET_SIZE + 1, 1)
                continue

            pid = ((pkt[1] & 0x1F) << 8) | pkt[2]
            if pid == target_pid:
                pcr = extract_pcr_ns(pkt)
                if pcr is not None:
                    pcr_data.append((pkt_count, pcr))
            pkt_count += 1

    if len(pcr_data) < 2:
        print("[!] No PCR data found.")
        sys.exit(1)

    print(f"[*] Found {len(pcr_data)} PCRs in {pkt_count} packets.")

    # Calculate bitrate based on PCR
    s_idx, s_pcr = pcr_data[0]
    e_idx, e_pcr = pcr_data[-1]
    duration_sec = (e_pcr - s_pcr) / 1e9
    if duration_sec > 0:
        bitrate = (e_idx - s_idx) * TS_PACKET_SIZE * 8 / duration_sec
        print(f"[*] Average Bitrate: {bitrate/1e6:.3f} Mbps")

    # Jitter Calc (Relative to ideal packet delivery)
    ideal_gap = Decimal(e_pcr - s_pcr) / Decimal(e_idx - s_idx)

    max_j = 0
    for idx, pcr in pcr_data:
        expected = s_pcr + int(Decimal(idx - s_idx) * ideal_gap)
        jitter = abs(pcr - expected)
        max_j = max(max_j, jitter)

    print(f"[*] Results: Max Deviation={max_j}ns")
    if max_j > MAX_JITTER_FILE_NS:
        print("[!] FAIL: High jitter detected for file source.")
        sys.exit(1)
    print("[+] PASS: PCR consistency verified.")

def analyze_pcapng(file_path, target_pid):
    print(f"[*] Lite PCAPNG Analysis: {file_path} (PID 0x{target_pid:04X})")

    pcr_data = []
    ts_resol = 10**-6 # Default microsecond

    with open(file_path, 'rb') as f:
        while True:
            raw_type = f.read(4)
            if not raw_type: break
            block_type = struct.unpack("<I", raw_type)[0]

            raw_len = f.read(4)
            if not raw_len: break
            block_len = struct.unpack("<I", raw_len)[0]

            if block_len < 12:
                print(f"[!] Invalid block length: {block_len}")
                break

            payload_len = block_len - 12
            payload = f.read(payload_len)
            f.read(4) # Skip trailing block_len

            if block_type == PCAPNG_IDB:
                if len(payload) > 16:
                    options = payload[16:]
                    idx = 0
                    while idx + 4 <= len(options):
                        opt_code, opt_len = struct.unpack("<HH", options[idx:idx+4])
                        if opt_code == 0: break # End of options
                        if opt_code == 9 and opt_len == 1:
                            ts_resol = 10 ** -options[idx+4]
                            break
                        idx += 4 + ((opt_len + 3) & ~3) # Padded to 32-bit

            elif block_type == PCAPNG_EPB:
                if len(payload) < 20: continue
                ts_high, ts_low, cap_len = struct.unpack("<III", payload[4:16])
                ticks = (ts_high << 32) | ts_low
                pcap_ns = int(Decimal(ticks) * Decimal(ts_resol) * Decimal(1e9))

                packet_data = payload[20:20+cap_len]
                # Assume standard Ethernet(14)+IP(20)+UDP(8) = 42 bytes offset for TS
                ts_payload = packet_data[42:]

                for i in range(0, len(ts_payload), TS_PACKET_SIZE):
                    pkt = ts_payload[i:i+TS_PACKET_SIZE]
                    if len(pkt) < 4: continue
                    pid = ((pkt[1] & 0x1F) << 8) | pkt[2]
                    if pid == target_pid:
                        pcr = extract_pcr_ns(pkt)
                        if pcr is not None: pcr_data.append((pcap_ns, pcr))

    if len(pcr_data) < 2:
        print("[!] No PCR data found.")
        sys.exit(1)

    # Simple Drift Compensation & Jitter Calc
    s_pcap, s_pcr = pcr_data[0]
    e_pcap, e_pcr = pcr_data[-1]
    duration_pcr = e_pcr - s_pcr
    if duration_pcr == 0: duration_pcr = 1
    drift = Decimal(e_pcap - s_pcap) / Decimal(duration_pcr)

    max_j, max_i, prev = 0, 0, s_pcap
    for cur_pcap, cur_pcr in pcr_data:
        max_i = max(max_i, cur_pcap - prev)
        prev = cur_pcap

        expected = s_pcap + int(Decimal(cur_pcr - s_pcr) * drift)
        max_j = max(max_j, abs(cur_pcap - expected))

    print(f"[*] Results: Jitter={max_j}ns, Interval={max_i/1e6:.2f}ms")
    if max_j > MAX_JITTER_PCAP_NS or max_i > MAX_INTERVAL_NS:
        print("[!] FAIL: Compliance violation detected.")
        sys.exit(1)
    print("[+] PASS: 100-Point Compliance Achieved.")

if __name__ == '__main__':
    p = argparse.ArgumentParser()
    p.add_argument('file')
    p.add_argument('--pid', type=lambda x: int(x,0), default=0x100)
    args = p.parse_args()

    with open(args.file, 'rb') as f:
        magic_bytes = f.read(4)
        if not magic_bytes:
            print("[!] Empty file.")
            sys.exit(1)
        magic = struct.unpack("<I", magic_bytes)[0]

        if magic == PCAPNG_SHB:
            analyze_pcapng(args.file, args.pid)
        else:
            analyze_raw_ts(args.file, args.pid)
