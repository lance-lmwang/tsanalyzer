#!/usr/bin/env python3
# [CORE ENGINE] Used for physical bitrate sampling based on PCR anchors.
# [INTERNAL DEPENDENCY] Required by tstd_promax_alignment_audit.sh.
# FEATURE: AUTO-DETECTION of PCR and Video PIDs.

import sys
import argparse
import numpy as np

TS_PACKET_SIZE = 188
TS_PACKET_BITS = 188 * 8
PCR_CLOCK = 27000000

def parse_pcr(pkt):
    if len(pkt) < TS_PACKET_SIZE: return None
    if pkt[0] != 0x47: return None
    afc = (pkt[3] >> 4) & 0x3
    if afc in (2, 3):
        afl = pkt[4]
        if afl > 0 and (pkt[5] & 0x10):
            pcr_base = (pkt[6] << 25) | (pkt[7] << 17) | (pkt[8] << 9) | (pkt[9] << 1) | (pkt[10] >> 7)
            pcr_ext = ((pkt[10] & 1) << 8) | pkt[11]
            return pcr_base * 300 + pcr_ext
    return None

def analyze_pcr_anchored_windows(args):
    """
    专家级物理层审计：PID 自动探测模式。
    """
    pcr_pid = args.pcr_pid
    vid_pid = args.vid_pid

    # --- Phase 0: Auto-Detection ---
    if pcr_pid is None or vid_pid is None:
        detected_pcr_pid = None
        with open(args.input, "rb") as f:
            for _ in range(10000): # Scan first 10k packets
                pkt = f.read(TS_PACKET_SIZE)
                if not pkt: break
                pcr = parse_pcr(pkt)
                if pcr is not None:
                    detected_pcr_pid = ((pkt[1] & 0x1F) << 8) | pkt[2]
                    break

        if detected_pcr_pid is None:
            print("0 0 0 0 0 0")
            return

        # If user didn't specify, use detected PCR PID for both
        if pcr_pid is None: pcr_pid = detected_pcr_pid
        if vid_pid is None: vid_pid = detected_pcr_pid

    # --- Phase 1: Physical Sampling ---
    pcr_anchors = []
    video_pid_bits = []

    with open(args.input, "rb") as f:
        packet_idx = 0
        while True:
            pkt = f.read(TS_PACKET_SIZE)
            if not pkt: break

            packet_idx += 1
            pid = ((pkt[1] & 0x1F) << 8) | pkt[2]
            pcr = parse_pcr(pkt)

            video_pid_bits.append(TS_PACKET_BITS if pid == vid_pid else 0)
            if pid == pcr_pid and pcr is not None:
                pcr_anchors.append((packet_idx, pcr))

    if len(pcr_anchors) < 32:
        print("0 0 0 0 0 0")
        return

    # 30-PCR 窗口计算
    window_size = 30
    bitrates = []

    for i in range(len(pcr_anchors) - window_size):
        start_idx, start_pcr = pcr_anchors[i]
        end_idx, end_pcr = pcr_anchors[i + window_size]
        bits_sum = sum(video_pid_bits[start_idx:end_idx])
        pcr_diff = end_pcr - start_pcr
        if pcr_diff < 0: pcr_diff += (1 << 33) * 300

        if pcr_diff > 0:
            rate_kbps = (bits_sum * PCR_CLOCK) / (pcr_diff * 1000.0)
            bitrates.append(rate_kbps)

    # 自动识别稳态点
    skip = len(bitrates) // 10
    steady_rates = bitrates[skip:] if len(bitrates) > skip else bitrates

    if not steady_rates:
        print("0 0 0 0 0 0")
        return

    mean_k = np.mean(steady_rates)
    max_k = np.max(steady_rates)
    min_k = np.min(steady_rates)
    delta_k = max_k - min_k
    std_k = np.std(steady_rates)
    rel_pct = (delta_k / mean_k) * 100.0 if mean_k > 0 else 0

    # 输出: MIN MAX DELTA AVG STD REL_PCT
    print(f"{min_k:.2f} {max_k:.2f} {delta_k:.2f} {mean_k:.2f} {std_k:.2f} {rel_pct:.2f}")

if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("input")
    ap.add_argument("--vid_pid", type=lambda x: int(x, 0), default=None)
    ap.add_argument("--pcr_pid", type=lambda x: int(x, 0), default=None)
    args = ap.parse_args()
    analyze_pcr_anchored_windows(args)
