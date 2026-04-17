#!/usr/bin/env python3
import sys
import argparse
import numpy as np
from collections import deque

TS_PACKET_SIZE = 188
TS_PACKET_BITS = 188 * 8
PCR_CLOCK = 27000000

def parse_pcr(pkt):
    if len(pkt) < TS_PACKET_SIZE: return None, None
    if pkt[0] != 0x47: return None, None
    pid = ((pkt[1] & 0x1F) << 8) | pkt[2]
    afc = (pkt[3] >> 4) & 0x3
    pcr = None
    if afc in (2, 3):
        afl = pkt[4]
        if afl > 0 and (pkt[5] & 0x10):
            pcr_base = (pkt[6] << 25) | (pkt[7] << 17) | (pkt[8] << 9) | (pkt[9] << 1) | (pkt[10] >> 7)
            pcr_ext = ((pkt[10] & 1) << 8) | pkt[11]
            pcr = pcr_base * 300 + pcr_ext
    return pid, pcr

def analyze_ts_precision(args):
    """
    基于物理槽位时间轴的上帝视角审计逻辑 (Tencent Expert Grade)
    """
    muxrate = args.muxrate
    t_slot = TS_PACKET_BITS / muxrate
    window_1s_len = int(1.0 / t_slot)

    video_bits_series = [] # 记录每一个槽位是否包含视频 PID
    pcr_stats = []

    packet_idx = 0
    with open(args.input, "rb") as f:
        while True:
            pkt = f.read(TS_PACKET_SIZE)
            if not pkt: break

            packet_idx += 1
            pid, pcr = parse_pcr(pkt)

            # 标记物理槽位载荷
            current_pid = ((pkt[1] & 0x1F) << 8) | pkt[2]
            video_bits_series.append(TS_PACKET_BITS if current_pid == args.vid_pid else 0)

            # 记录 PCR 物理偏差
            if pid == args.pcr_pid and pcr is not None:
                ideal_pcr = packet_idx * t_slot * PCR_CLOCK
                pcr_stats.append(pcr - ideal_pcr)

    # --- 统计核心 ---
    video_bits_np = np.array(video_bits_series)

    # 使用滑动窗口计算 1s 均值序列 (卷积实现，速度最快)
    if len(video_bits_np) < window_1s_len:
        print("0 0 0 0 0 0 0 0")
        return

    # 1s 窗口滑动求和
    window = np.ones(window_1s_len)
    bitrate_1s = np.convolve(video_bits_np, window, 'valid') / 1.0 # bits per 1s

    # 跳过起始 5s 预热
    warmup_offset = int(5.0 / t_slot)
    if len(bitrate_1s) > warmup_offset:
        steady_bitrates = bitrate_1s[warmup_offset:]
    else:
        steady_bitrates = bitrate_1s

    # 计算指标
    mean_k = np.mean(steady_bitrates) / 1000
    max_k = np.max(steady_bitrates) / 1000
    min_k = np.min(steady_bitrates) / 1000
    dev_k = max(max_k - mean_k, mean_k - min_k)
    std_k = np.std(steady_bitrates) / 1000

    # 客户关注的最终评分: Score = Fluctuation + Std * 2
    fluct_k = max_k - min_k
    score = fluct_k + std_k * 2.0

    # PCR Jitter (ns)
    pcr_jitter_ticks = np.std(pcr_stats) if pcr_stats else 0
    pcr_jitter_ns = (pcr_jitter_ticks * 1000000000) / PCR_CLOCK

    # 输出 8 个对齐字段
    # MEANk MAXk MINk ±DEVk STDk SCORE PCR_JIT_NS PKT_COUNT
    print(f"{mean_k:.2f} {max_k:.2f} {min_k:.2f} {dev_k:.2f} {std_k:.2f} {score:.2f} {pcr_jitter_ns:.2f} {packet_idx}")

    # 在 stderr 打印详细分析 (不干扰 Bash 解析)
    print(f"\n[PROMAX-TS-ANALYZER-EMULATION]", file=sys.stderr)
    print(f"  Steady samples : {len(steady_bitrates)} seconds-windows", file=sys.stderr)
    print(f"  1s Fluctuation : {fluct_k:.2f} kbps", file=sys.stderr)
    print(f"  Bitrate StdDev : {std_k:.2f} kbps", file=sys.stderr)
    print(f"  Quality Score  : {score:.2f} (Target < 50.0)", file=sys.stderr)
    print(f"  PCR Jitter RMS : {pcr_jitter_ns:.2f} ns (Target < 500ns)", file=sys.stderr)

if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("input")
    ap.add_argument("--vid_pid", type=lambda x: int(x, 0), default=0x21)
    ap.add_argument("--pcr_pid", type=lambda x: int(x, 0), default=0x21)
    ap.add_argument("--muxrate", type=float, default=1200000.0)
    args = ap.parse_args()
    analyze_ts_precision(args)
