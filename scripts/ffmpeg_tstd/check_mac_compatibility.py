#!/usr/bin/env python3
import sys
import subprocess
import json
import os

def find_ffprobe():
    script_dir = os.path.dirname(os.path.abspath(__file__))
    root_dir = os.path.abspath(os.path.join(script_dir, "../../"))
    ffmpeg_root = os.path.abspath(os.path.join(root_dir, "../ffmpeg.wz.master"))
    project_ffprobe = os.path.join(ffmpeg_root, "ffdeps_img/ffmpeg/bin/ffprobe")
    if os.path.exists(project_ffprobe):
        return project_ffprobe
    return "ffprobe"

def check_interleave(ts_file):
    ffprobe_bin = find_ffprobe()
    print(f"[*] Auditing A/V Interleave & Scheduling Sanity for: {ts_file}")

    cmd = [
        ffprobe_bin, "-v", "error",
        "-show_packets",
        "-show_entries", "packet=codec_type,dts_time,pos",
        "-of", "json",
        ts_file
    ]

    try:
        result = subprocess.run(cmd, capture_output=True, text=True, check=True)
        data = json.loads(result.stdout)
    except Exception as e:
        print(f"[ERROR] Failed to probe file: {e}")
        return False

    packets = data.get('packets', [])
    if not packets:
        print("[ERROR] No packets found. Stream is empty.")
        return False

    max_drift = 0
    v_last_dts = None
    a_last_dts = None

    # 饥饿检测 (Starvation Detection)
    window_size = 1000
    v_count_in_win = 0
    a_count_in_win = 0
    starvation_detected = False

    # 物理分布检测
    last_audio_pos = None
    max_audio_gap_bytes = 0

    total_audited = len(packets)

    for i, pkt in enumerate(packets):
        t = pkt.get('codec_type')
        dts = pkt.get('dts_time')
        pos = pkt.get('pos')

        # 统计窗口内的分布
        if t == 'video': v_count_in_win += 1
        elif t == 'audio': a_count_in_win += 1

        if i >= window_size:
            prev_pkt = packets[i - window_size]
            if prev_pkt.get('codec_type') == 'video': v_count_in_win -= 1
            elif prev_pkt.get('codec_type') == 'audio': a_count_in_win -= 1

            # 如果连续 1000 个包中某类流完全消失，判定为调度事故
            if (v_count_in_win == 0 or a_count_in_win == 0) and not starvation_detected:
                print(f"[FAIL] Stream Starvation Detected near packet {i}! One stream is completely blocked.")
                starvation_detected = True

        if dts == 'N/A' or dts is None: continue

        try:
            dts = float(dts)
            pos = int(pos) if pos and pos != 'N/A' else None
        except ValueError: continue

        if t == 'video':
            v_last_dts = dts
        elif t == 'audio':
            a_last_dts = dts
            if last_audio_pos is not None and pos is not None:
                gap = pos - last_audio_pos
                if gap > max_audio_gap_bytes: max_audio_gap_bytes = gap
            last_audio_pos = pos

        if v_last_dts is not None and a_last_dts is not None:
            drift = abs(v_last_dts - a_last_dts)
            if drift > max_drift: max_drift = drift

    print(f"    - Max A/V DTS Drift: {max_drift*1000:.2f} ms")
    print(f"    - Max Audio Physical Gap: {max_audio_gap_bytes / 1024:.2f} KB")
    print(f"    - Starvation Status: {'FAILED' if starvation_detected else 'PASSED'}")

    is_safe = not starvation_detected
    if max_drift > 1.5:
        print(f"[FAIL] A/V Interleave ({max_drift*1000:.2f}ms) exceeds limit!")
        is_safe = False
    if max_audio_gap_bytes > 128 * 1024:
        print(f"[FAIL] Physical Audio Gap ({max_audio_gap_bytes/1024:.2f}KB) too large!")
        is_safe = False

    return is_safe

if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Usage: check_mac_compatibility.py <input.ts>")
        sys.exit(1)
    sys.exit(0 if check_interleave(sys.argv[1]) else 1)
