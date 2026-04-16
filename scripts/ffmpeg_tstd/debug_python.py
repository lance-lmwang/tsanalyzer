#!/usr/bin/env python3
import sys

TS_PACKET_SIZE = 188
vid_pid = 0x21

def extract_pcr(packet):
    afc = (packet[3] >> 4) & 0x03
    if afc == 2 or afc == 3:
        af_len = packet[4]
        if af_len >= 7:
            flags = packet[5]
            if flags & 0x10:
                base = (packet[6] << 25) | (packet[7] << 17) | (packet[8] << 9) | (packet[9] << 1) | (packet[10] >> 7)
                ext = ((packet[10] & 0x01) << 8) | packet[11]
                return (base * 300 + ext) / 27000000.0
    return None

with open("/home/lmwang/dev/cae/tsanalyzer/output/promax_matrix/m1.ts", 'rb') as f:
    last_pcr = None
    pkt_count = 0
    vid_count = 0
    for _ in range(50000): # first 50k packets ~ 8 seconds
        pkt = f.read(TS_PACKET_SIZE)
        if not pkt: break
        if pkt[0] != 0x47: continue

        pid = ((pkt[1] & 0x1F) << 8) | pkt[2]
        pkt_count += 1
        if pid == vid_pid: vid_count += 1

        pcr = extract_pcr(pkt)
        if pcr is not None:
            if last_pcr is not None:
                dt = pcr - last_pcr
                print(f"PCR at packet {pkt_count}: dt={dt*1000:.3f}ms, num_pkts={pkt_count}, vid_ratio={vid_count/pkt_count:.3f}")
            last_pcr = pcr
            pkt_count = 0
            vid_count = 0
