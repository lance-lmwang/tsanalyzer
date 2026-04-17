#!/usr/bin/env python3
import sys
import argparse
import signal
import numpy as np
from collections import deque

signal.signal(signal.SIGPIPE, signal.SIG_DFL)

TS_PACKET_SIZE = 188
TS_PACKET_BITS = TS_PACKET_SIZE * 8
PCR_CLOCK = 27000000.0
MAX_PCR = 1 << 42


# =========================
# PCR 工具
# =========================
def get_pcr(pkt):
    if len(pkt) < 12: return None
    afc = (pkt[3] >> 4) & 0x3
    if afc in (2, 3):
        afl = pkt[4]
        if afl >= 7 and (pkt[5] & 0x10):
            b = pkt[6:12]
            base = (b[0]<<25)|(b[1]<<17)|(b[2]<<9)|(b[3]<<1)|(b[4]>>7)
            ext  = ((b[4]&1)<<8)|b[5]
            return base*300 + ext
    return None


def pcr_delta(a, b):
    d = a - b
    if d > MAX_PCR//2:
        d -= MAX_PCR
    elif d < -MAX_PCR//2:
        d += MAX_PCR
    return d


# =========================
# PCR PID 检测（单调性评分）
# =========================
def detect_pcr_pid_strict(ts):
    stats = {}

    with open(ts, "rb") as f:
        for _ in range(20000):
            pkt = f.read(TS_PACKET_SIZE)
            if not pkt or pkt[0] != 0x47:
                continue

            pid = ((pkt[1]&0x1F)<<8)|pkt[2]
            pcr = get_pcr(pkt)
            if pcr is None:
                continue

            if pid not in stats:
                stats[pid] = {"last": pcr, "score": 0}
                continue

            d = pcr_delta(pcr, stats[pid]["last"])

            # 合法 PCR 间隔：0 ~ 100ms
            if 0 < d < 0.1 * PCR_CLOCK:
                stats[pid]["score"] += 1
            else:
                stats[pid]["score"] -= 5

            stats[pid]["last"] = pcr

    if not stats:
        return None

    return max(stats.items(), key=lambda x: x[1]["score"])[0]


# =========================
# 连续时间窗口（关键升级：梯形积分模型）
# =========================
class ContinuousWindow:
    def __init__(self, size_sec):
        self.size_ticks = size_sec * PCR_CLOCK
        self.samples = deque()  # (pcr, rate)

    def add(self, pcr, rate):
        self.samples.append((pcr, rate))

        while self.samples and pcr_delta(pcr, self.samples[0][0]) > self.size_ticks:
            self.samples.popleft()

    def get_rate(self):
        if len(self.samples) < 2:
            return 0

        total_bits_weighted = 0
        for i in range(1, len(self.samples)):
            p0, r0 = self.samples[i-1]
            p1, r1 = self.samples[i]

            dt = pcr_delta(p1, p0) / PCR_CLOCK
            if dt <= 0:
                continue

            # 梯形积分：计算该时间段内的有效位流贡献
            total_bits_weighted += (r0 + r1) / 2 * dt

        duration = pcr_delta(self.samples[-1][0], self.samples[0][0]) / PCR_CLOCK
        if duration <= 0:
            return 0

        return total_bits_weighted / duration / 1000.0


# =========================
# 主流程
# =========================
def main():
    parser = argparse.ArgumentParser(description="T-STD Professional Auditor v7.0 - First-Class Metrology")
    parser.add_argument("input")
    parser.add_argument("--vid", type=lambda x:int(x,0), default=0x21)
    parser.add_argument("--target", type=float, required=True, help="Target Video Bitrate (kbps)")
    parser.add_argument("--simple", action="store_true", help="Output summary only for shell table")
    args = parser.parse_args()

    pcr_pid = detect_pcr_pid_strict(args.input)
    if pcr_pid is None:
        print("ERROR: No Valid PCR PID found")
        sys.exit(1)

    v_pid = args.vid if args.vid else pcr_pid

    if not args.simple:
        print(f"[*] Broadcast-Grade Audit: {args.input}")
        print(f"[*] PCR_PID: 0x{pcr_pid:04x}, Video_PID: 0x{v_pid:04x}, Target: {args.target}k")
        print("-" * 75)
        print(f"{'Time':>8} | {'Vid_Rate(k)':>15} | {'Min_Dip(k)':>12} | {'Status'}")
        print("-" * 75)

    win_1s = ContinuousWindow(1.0)
    win_100ms = ContinuousWindow(0.1)

    bits_all = 0
    bits_vid = 0
    last_pcr = None
    min_dip = 9999.0
    last_sec = -1

    all_steady_samples = []

    with open(args.input, "rb") as f:
        while True:
            pkt = f.read(TS_PACKET_SIZE)
            if not pkt: break
            if pkt[0] != 0x47: continue

            pid = ((pkt[1]&0x1F)<<8)|pkt[2]
            bits_all += TS_PACKET_BITS
            if pid == v_pid:
                bits_vid += TS_PACKET_BITS

            if pid == pcr_pid:
                pcr = get_pcr(pkt)
                if pcr is None: continue

                if last_pcr is not None:
                    dt = pcr_delta(pcr, last_pcr) / PCR_CLOCK
                    if dt > 0:
                        rate_vid = bits_vid / dt
                        win_1s.add(pcr, rate_vid)
                        win_100ms.add(pcr, rate_vid)

                        bits_all = 0
                        bits_vid = 0

                        r1 = win_1s.get_rate()
                        r100 = win_100ms.get_rate()

                        if r100 > 0:
                            min_dip = min(min_dip, r100)

                        if pcr / PCR_CLOCK > 2.0 and r1 > 10:
                            all_steady_samples.append(r1)

                        sec = int(pcr / PCR_CLOCK)
                        if sec > last_sec:
                            if not args.simple:
                                status = "OK"
                                if r1 < 10: status = "!!! STALL !!!"
                                elif min_dip < args.target * 0.6: status = "!!! DIP !!!"
                                elif r1 < args.target * 0.9: status = "! LOW !"
                                print(f"{sec:7d}s | {r1:15.2f} | {min_dip:12.2f} | {status}")

                            min_dip = 9999.0
                            last_sec = sec

                last_pcr = pcr

    if args.simple:
        if not all_steady_samples:
            print("0.00 0.00 0.00 0.00 0.00")
        else:
            mean = np.mean(all_steady_samples)
            mx = np.max(all_steady_samples)
            mn = np.min(all_steady_samples)
            std = np.std(all_steady_samples)
            score = (mx - mn) + 2.0 * std
            print(f"{mean:.2f} {mx:.2f} {mn:.2f} {std:.2f} {score:.2f}")

if __name__ == "__main__":
    main()
