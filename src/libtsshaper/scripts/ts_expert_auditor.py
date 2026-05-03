#!/usr/bin/env python3
# [INTERNAL DEPENDENCY] Required by multiple T-STD regression harnesses.
# [CORE ENGINE] Used for physical bitrate sampling and TR 101 290 compliance.
import sys
import argparse
import signal
import numpy as np

signal.signal(signal.SIGPIPE, signal.SIG_DFL)

TS_PACKET_SIZE = 188
PCR_CLOCK = 27000000.0
MAX_PCR = 1 << 42

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
    if d > MAX_PCR//2: d -= MAX_PCR
    elif d < -MAX_PCR//2: d += MAX_PCR
    return d

def main():
    parser = argparse.ArgumentParser(description="T-STD Shadow Auditor - Aligned with tstd.c Statistics")
    parser.add_argument("input")
    parser.add_argument("--vid", type=lambda x:int(x,0), default=0x21)
    parser.add_argument("--target", type=float, required=True, help="Target Video Bitrate (kbps)")
    parser.add_argument("--window", type=int, default=30, help="PCR Window Size")
    parser.add_argument("--simple", action="store_true")
    parser.add_argument("--skip", type=float, default=5.0)
    args = parser.parse_args()

    v_pid = args.vid
    pcr_win_size = args.window

    window_rates_video = []
    window_rates_ts = []

    # State Tracking (Shadowing T-STD Logic)
    pcr_count = 0
    win_start_pcr = None
    win_start_v_bytes = 0
    win_start_ts_bytes = 0

    total_v_bytes = 0
    total_ts_bytes = 0

    with open(args.input, "rb") as f:
        while True:
            pkt = f.read(TS_PACKET_SIZE)
            if not pkt: break
            if pkt[0] != 0x47: continue

            total_ts_bytes += TS_PACKET_SIZE
            pid = ((pkt[1]&0x1F)<<8)|pkt[2]
            if pid == v_pid:
                total_v_bytes += TS_PACKET_SIZE

            pcr = get_pcr(pkt)
            if pcr is not None:
                if win_start_pcr is None:
                    win_start_pcr = pcr
                    win_start_v_bytes = total_v_bytes
                    win_start_ts_bytes = total_ts_bytes
                    pcr_count = 0
                else:
                    pcr_count += 1
                    if pcr_count >= pcr_win_size:
                        duration = pcr_delta(pcr, win_start_pcr) / PCR_CLOCK
                        if duration > 0:
                            # 1. 影子统计：计算视频负载瞬时码率
                            v_bits = (total_v_bytes - win_start_v_bytes) * 8
                            v_rate = (v_bits / duration) / 1000.0

                            # 2. 影子统计：计算系统总物理码率（验证 CBR）
                            ts_bits = (total_ts_bytes - win_start_ts_bytes) * 8
                            ts_rate = (ts_bits / duration) / 1000.0

                            curr_time = pcr / PCR_CLOCK
                            if curr_time > args.skip:
                                window_rates_video.append(v_rate)
                                window_rates_ts.append(ts_rate)

                        # 同步重置锚点 (与 tstd.c 逻辑完全一致)
                        win_start_pcr = pcr
                        win_start_v_bytes = total_v_bytes
                        win_start_ts_bytes = total_ts_bytes
                        pcr_count = 0

    if not window_rates_video:
        print("0 0 0 0 0")
        return

    v_rates = np.array(window_rates_video)
    ts_rates = np.array(window_rates_ts)

    mean_v = np.mean(v_rates)
    max_v = np.max(v_rates)
    min_v = np.min(v_rates)
    std_v = np.std(v_rates)

    # 我们用视频的平滑度评分
    score = std_v * 3.0 + abs(mean_v - args.target)

    if args.simple:
        # 对齐 master_audit.sh 的期望输出格式
        print(f"{mean_v:.2f} {max_v:.2f} {min_v:.2f} {std_v:.2f} {score:.2f}")
    else:
        print(f"--- T-STD Shadow Audit (PCR-Window: {pcr_win_size}) ---")
        print(f"Video PID : 0x{v_pid:04x}")
        print(f"Video Rate: Mean:{mean_v:7.2f} Max:{max_v:7.2f} Min:{min_v:7.2f} Delta:{max_v-min_v:7.2f} kbps")
        print(f"TS Total  : Mean:{np.mean(ts_rates):7.2f} Max:{np.max(ts_rates):7.2f} Min:{np.min(ts_rates):7.2f} kbps")
        print(f"StdDev(V) : {std_v:7.2f} kbps")

if __name__ == "__main__":
    main()
