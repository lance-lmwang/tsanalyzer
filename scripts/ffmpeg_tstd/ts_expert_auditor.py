#!/usr/bin/env python3
import sys
import argparse
import signal
import numpy as np

signal.signal(signal.SIGPIPE, signal.SIG_DFL)

TS_PACKET_SIZE = 188
TS_PACKET_BITS = TS_PACKET_SIZE * 8
PCR_CLOCK = 27000000.0

def get_pcr_precise(pkt):
    """
    Standard PCR Extraction (Fixed Alignment)
    PCR (48 bits) = base (33 bits) + reserved (6 bits) + extension (9 bits)
    """
    if len(pkt) < 12: return None
    afc = (pkt[3] >> 4) & 0x3
    if afc in (2, 3):
        afl = pkt[4]
        if afl < 7: return None
        if pkt[5] & 0x10:
            # 严格读取 6 字节: pkt[6..11]
            b = pkt[6:12]
            # Base (33 bits)
            base = (b[0] << 25) | (b[1] << 17) | (b[2] << 9) | (b[3] << 1) | (b[4] >> 7)
            # Ext (9 bits)
            ext = ((b[4] & 0x01) << 8) | b[5]
            return base * 300 + ext
    return None

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("input")
    parser.add_argument("--vid", type=lambda x: int(x,0), default=0x21)
    parser.add_argument("--target", type=float, required=True)
    parser.add_argument("--simple", action="store_true")
    args = parser.parse_args()

    total_vid_bits_abs = 0
    total_all_bits_abs = 0
    pcr_buffer = []
    all_steady_vbr = []
    pcr_count = 0

    last_report_pcr = None

    with open(args.input, "rb") as f:
        while True:
            pkt = f.read(TS_PACKET_SIZE)
            if not pkt: break
            if pkt[0] != 0x47: continue
            pid = ((pkt[1] & 0x1F) << 8) | pkt[2]

            total_all_bits_abs += TS_PACKET_BITS
            if pid == args.vid:
                total_vid_bits_abs += TS_PACKET_BITS

            pcr = get_pcr_precise(pkt)
            if pcr is not None:
                pcr_count += 1
                pcr_buffer.append((pcr, total_all_bits_abs, total_vid_bits_abs))

                if len(pcr_buffer) > 5: # 增加点稳定性，group=5 (约200ms)
                    start_pcr, start_total, start_vid = pcr_buffer.pop(0)
                    dt = pcr - start_pcr
                    if dt < 0: dt += (1 << 33) * 300
                    if dt > 0:
                        sec = dt / PCR_CLOCK
                        vbr_sample = ((total_vid_bits_abs - start_vid) / sec) / 1000.0

                        # 修正 2: 预热期缩短到 2.0s
                        if pcr / PCR_CLOCK > 2.0:
                            all_steady_vbr.append(vbr_sample)

                        if not args.simple and (last_report_pcr is None or (pcr - last_report_pcr >= 27000000)):
                            print(f"Time: {pcr/PCR_CLOCK:8.2f}s | Vid_VBR: {vbr_sample:8.2f}k | Samples: {len(all_steady_vbr)}")
                            last_report_pcr = pcr

    if args.simple:
        if not all_steady_vbr:
            # 即使失败也输出 0，防止 shell read 挂起
            print("0.00 0.00 0.00 0.00 0.00")
        else:
            mean = np.mean(all_steady_vbr)
            mx = np.max(all_steady_vbr)
            mn = np.min(all_steady_vbr)
            std = np.std(all_steady_vbr)
            score = (mx - mn) + 2.0 * std
            print(f"{mean:.2f} {mx:.2f} {mn:.2f} {std:.2f} {score:.2f}")
    elif pcr_count == 0:
        print("ERROR: No PCR found! Check PID or Parser.")

if __name__ == "__main__":
    main()
