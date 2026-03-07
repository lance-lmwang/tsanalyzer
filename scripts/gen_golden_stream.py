import os, import sys
import socket
import time

def make_packet(pid, payload, pusi=False, cc=0, pcr=None):
    h = [0x47, (0x40 if pusi else 0x00) | ((pid >> 8) & 0x1F), pid & 0xFF, 0x10 | (cc & 0x0F)]
    if pcr is not None:
        h[3] |= 0x20
        pcr_base = pcr // 300
        pcr_ext = pcr % 300
        af = [7, 0x10, (pcr_base >> 25) & 0xFF, (pcr_base >> 17) & 0xFF, (pcr_base >> 9) & 0xFF, 
               (pcr_base >> 1) & 0xFF, ((pcr_base & 1) << 7) | ((pcr_ext >> 8) & 1), pcr_ext & 0xFF]
        return bytes(h) + bytes(af) + payload + b'\xff' * (188 - 4 - 8 - len(payload))
    return bytes(h) + payload + b'\xff' * (188 - 4 - len(payload))

# Standard Compliant Payloads
pat = b'\x00\x00\xb0\x0d\x00\x01\xc1\x00\x00\x00\x01\xf0\x00\x2a\xb1\x04\xb2'
pmt = b'\x02\xb0\x12\x00\x01\xc1\x00\x00\xe1\x00\xf0\x00\x1b\xe1\x00\xf0\x00\x2a\xb1\x04\xb2'

def stream(port):
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    dest = ('127.0.0.1', port)
    ccs = {0:0, 0x1000:0, 0x0100:0}
    pcr = 0
    count = 0
    
    # 2Mbps = 1330 packets/sec
    interval = 1.0 / 1330
    
    while True:
        pkts = []
        for _ in range(7): # Standard 7 TS per UDP
            if count % 100 == 0:
                pkts.append(make_packet(0, pat, True, ccs[0]))
                ccs[0] = (ccs[0] + 1) % 16
            elif count % 100 == 1:
                pkts.append(make_packet(0x1000, pmt, True, ccs[0x1000]))
                ccs[0x1000] = (ccs[0x1000] + 1) % 16
            else:
                p_pcr = pcr if (count % 20 == 0) else None
                pkts.append(make_packet(0x0100, b'', False, ccs[0x0100], p_pcr))
                ccs[0x0100] = (ccs[0x0100] + 1) % 16
                pcr += 10000
            count += 1
        
        sock.sendto(b''.join(pkts), dest)
        time.sleep(interval * 7)

if __name__ == "__main__":
    stream(int(sys.argv[1]))
