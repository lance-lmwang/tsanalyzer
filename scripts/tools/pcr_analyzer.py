#!/usr/bin/env python3
"""
TR 101 290 Nanosecond PCR Jitter Analyzer (Multi-Format Support)
Validates the physical layer timing precision of libtsshaper output.
Supports both Classic PCAP (nanosecond magic) and PCAPNG.
"""

import sys
import argparse
import struct
from decimal import Decimal
import os

try:
    # We'll use the generic PcapReader which handles format detection
    from scapy.all import PcapReader, UDP
except ImportError:
    print("[!] Error: 'scapy' library not found. Please install it with 'pip install scapy'")
    sys.exit(1)

# Attempt to load plotting libraries
HAS_PLOT = False
try:
    import plotly.graph_objects as go
    HAS_PLOT = True
except ImportError:
    pass

# =============================================================================
# Constants & Thresholds
# =============================================================================
TS_PACKET_SIZE = 188
SYNC_BYTE = 0x47
DVB_CLOCK_FREQ = 27_000_000  # 27 MHz

# Strict compliance thresholds (in nanoseconds)
MAX_JITTER_NS = 30           # Our hyper-strict engine goal
MAX_INTERVAL_NS = 40_000_000 # 40ms limit for PCR repetition

def extract_pcr_ns(ts_pkt: bytes) -> int:
    if len(ts_pkt) < 12 or ts_pkt[0] != SYNC_BYTE:
        return None
    afc = (ts_pkt[3] >> 4) & 0x03
    if afc < 2:
        return None
    af_length = ts_pkt[4]
    if af_length < 7:
        return None
    if not (ts_pkt[5] & 0x10):
        return None

    # Unpack 6 bytes of PCR (bytes 6 to 11)
    pcr_h, pcr_l = struct.unpack(">IH", ts_pkt[6:12])
    pcr_base = (pcr_h << 1) | (pcr_l >> 15)
    pcr_ext = pcr_l & 0x1FF

    total_ticks = (pcr_base * 300) + pcr_ext
    pcr_ns = int((total_ticks * 1000) // 27)
    return pcr_ns

def analyze_capture(file_path: str, target_pid: int, plot_file: str = None):
    print(f"[*] Analyzing Capture: {file_path} for PCR PID: 0x{target_pid:04X}")

    pcr_data = [] # List of (pcap_ns, pcr_ns)
    packet_count = 0

    try:
        # PcapReader handles both classic PCAP (including nano) and PCAPNG
        with PcapReader(file_path) as pcap:
            for packet in pcap:
                packet_count += 1

                # Scapy's packet.time is a Decimal that respects the PCAP/PCAPNG resolution
                current_pcap_ns = int(packet.time * Decimal('1e9'))

                if not packet.haslayer(UDP):
                    continue

                payload = bytes(packet[UDP].payload)

                for i in range(0, len(payload), TS_PACKET_SIZE):
                    ts_pkt = payload[i:i+TS_PACKET_SIZE]
                    if len(ts_pkt) < 4 or ts_pkt[0] != SYNC_BYTE:
                        continue

                    pid = ((ts_pkt[1] & 0x1F) << 8) | ts_pkt[2]
                    if pid != target_pid:
                        continue

                    pcr_ns = extract_pcr_ns(ts_pkt)
                    if pcr_ns is not None:
                        pcr_data.append((current_pcap_ns, pcr_ns))

    except Exception as e:
        print(f"[!] Error reading capture: {e}")
        sys.exit(1)

    print(f"[*] Total packets scanned: {packet_count}")
    print(f"[*] Total PCRs extracted:  {len(pcr_data)}")

    if len(pcr_data) < 2:
        print("[!] Error: Insufficient PCR data for analysis.")
        sys.exit(1)

    # Advanced Jitter Analysis (Drift Compensation)
    start_pcap, start_pcr = pcr_data[0]
    end_pcap, end_pcr = pcr_data[-1]

    duration_pcr = end_pcr - start_pcr
    if duration_pcr < 0: # Wrap-around
        duration_pcr += int((1 << 33) * 300 * 1000 // 27)

    drift_ratio = Decimal(end_pcap - start_pcap) / Decimal(duration_pcr if duration_pcr != 0 else 1)

    jitters = []
    intervals = []
    prev_pcap = start_pcap
    max_jitter = 0
    max_interval = 0

    for pcap_ns, pcr_ns in pcr_data:
        interval = pcap_ns - prev_pcap
        if interval > max_interval: max_interval = interval
        intervals.append(interval)
        prev_pcap = pcap_ns

        pcr_delta = pcr_ns - start_pcr
        if pcr_delta < -1e12: # Wrap-around
             pcr_delta += int((1 << 33) * 300 * 1000 // 27)

        expected_ns = start_pcap + int(Decimal(pcr_delta) * drift_ratio)
        jitter = int(pcap_ns - expected_ns)
        jitters.append(jitter)

        abs_jitter = abs(jitter)
        if abs_jitter > max_jitter:
            max_jitter = abs_jitter

    print(f"[*] Clock Drift Ratio:    {drift_ratio:.12f}")
    print(f"[*] Max PCR Interval:     {max_interval / 1e6:.3f} ms")
    print(f"[*] Max PCR Jitter:       {max_jitter} ns")

    if plot_file and HAS_PLOT:
        print(f"[*] Generating Jitter Plot: {plot_file}")
        fig = go.Figure()
        times_ms = [(p[0] - start_pcap) / 1e6 for p in pcr_data]
        fig.add_trace(go.Scatter(x=times_ms, y=jitters, mode='lines+markers', name='PCR Jitter (ns)'))
        fig.update_layout(title=f"PCR Jitter Analysis - PID 0x{target_pid:04X}",
                          xaxis_title="Time (ms)", yaxis_title="Jitter (ns)", template="plotly_dark")
        fig.write_html(plot_file)

    failed = False
    if max_interval > MAX_INTERVAL_NS:
        print(f"[!] FAIL: PCR Interval ({max_interval / 1e6:.3f} ms) > 40ms")
        failed = True
    if max_jitter > MAX_JITTER_NS:
        print(f"[!] FAIL: PCR Jitter ({max_jitter} ns) exceeded {MAX_JITTER_NS} ns")
        failed = True

    if failed:
        sys.exit(1)

    print("[+] PASS: 100-Point Compliance Achieved.")
    sys.exit(0)

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description='Advanced PCR Jitter Analyzer')
    parser.add_argument('file', help='Path to the capture file')
    parser.add_argument('--pid', type=lambda x: int(x, 0), default=0x100, help='PCR PID (default: 0x100)')
    parser.add_argument('--plot', help='Generate HTML plot')

    args = parser.parse_args()
    analyze_capture(args.file, args.pid, args.plot)
