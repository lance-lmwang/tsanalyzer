#!/usr/bin/env python3
import subprocess
import json
import sys

def analyze_source_interleaving(file_path, ffprobe_path):
    print(f"[*] Analyzing source file: {file_path}")
    cmd = [
        ffprobe_path,
        "-v", "quiet",
        "-print_format", "json",
        "-show_packets",
        "-read_intervals", "%+#500", # Read first 500 packets
        file_path
    ]

    result = subprocess.run(cmd, capture_output=True, text=True)
    if result.returncode != 0:
        print("ffprobe failed.")
        return

    data = json.loads(result.stdout)
    packets = data.get('packets', [])

    first_audio = None
    first_video = None
    first_data = None

    audio_count_before_video = 0

    for pkt in packets:
        codec_type = pkt.get('codec_type')
        dts_time = float(pkt.get('dts_time', 0))

        if codec_type == 'audio':
            if first_audio is None:
                first_audio = dts_time
            if first_video is None:
                audio_count_before_video += 1
        elif codec_type == 'video':
            if first_video is None:
                first_video = dts_time
        elif codec_type == 'data':
            if first_data is None:
                first_data = dts_time

        if first_audio and first_video and first_data:
            # Keep counting audio before video though
            pass

    print("\n--- Timestamp Evidence (First Packets) ---")
    if first_data is not None:
        print(f"First DATA (EPG) DTS:   {first_data:.6f} s")
    if first_audio is not None:
        print(f"First AUDIO DTS:        {first_audio:.6f} s")
    if first_video is not None:
        print(f"First VIDEO DTS:        {first_video:.6f} s")

    if first_audio is not None and first_video is not None:
        diff = first_video - first_audio
        print(f"\n=> SKEW: Video is {diff:.6f} seconds LATE compared to Audio.")
        print(f"=> BURST: There are {audio_count_before_video} Audio packets physically located BEFORE the first Video packet.")

if __name__ == '__main__':
    ffprobe_path = "/home/lmwang/dev/cae/ffmpeg.wz.master/ffdeps_img/ffmpeg/bin/ffprobe"
    target_file = "/home/lmwang/dev/cae/sample/knet_sd_03.ts"
    analyze_source_interleaving(target_file, ffprobe_path)
