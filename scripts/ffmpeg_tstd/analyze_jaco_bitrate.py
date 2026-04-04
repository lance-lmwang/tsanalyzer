import sys
import subprocess

def analyze_pcr_bitrate(file_path):
    print(f"[*] Analyzing real bitrate for: {file_path}")

    # ffprobe command to dump all packet DTS (as a proxy for PCR/timing)
    cmd = [
        "../../../ffmpeg.wz.master/ffdeps_img/ffmpeg/bin/ffprobe",
        "-v", "error",
        "-select_streams", "v:0",
        "-show_entries", "packet=pos,dts",
        "-of", "compact=p=0:nk=1",
        file_path
    ]

    process = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)

    last_dts = -1
    total_duration_ticks = 0
    total_bytes = 0
    jumps = 0
    first_dts = -1

    # Track logical segments
    segment_start_dts = -1
    segment_last_dts = -1
    segment_start_pos = -1
    segment_last_pos = -1

    for line in process.stdout:
        try:
            parts = line.strip().split('|')
            if len(parts) < 2: continue
            dts = int(parts[0])
            pos = int(parts[1])

            if first_dts == -1:
                first_dts = dts
                segment_start_dts = dts
                segment_start_pos = pos

            # Detect rollback or huge jump (> 1 second = 90000 ticks)
            if last_dts != -1 and (dts < last_dts or (dts - last_dts) > 180000):
                # End current segment
                dur = segment_last_dts - segment_start_dts
                size = segment_last_pos - segment_start_pos
                if dur > 0:
                    total_duration_ticks += dur
                    # We use physical size difference as a proxy for bits
                    # Note: this is rough because it's only video packets

                print(f"[!] Discontinuity detected at pos {pos}: {last_dts} -> {dts} (Delta: {dts-last_dts})")
                jumps += 1
                segment_start_dts = dts
                segment_start_pos = pos

            segment_last_dts = dts
            segment_last_pos = pos
            last_dts = dts

        except ValueError:
            continue

    # Final segment
    dur = segment_last_dts - segment_start_dts
    if dur > 0:
        total_duration_ticks += dur

    # Get total file size for accurate bitrate
    import os
    file_size = os.path.getsize(file_path)

    if total_duration_ticks > 0:
        total_duration_sec = total_duration_ticks / 90000.0
        # Average Bitrate = (File Size in bits) / (Sum of continuous durations)
        avg_bitrate_bps = (file_size * 8) / total_duration_sec

        print("\n=== Analysis Result ===")
        print(f"[*] Total File Size: {file_size} bytes")
        print(f"[*] Total Logical Duration (Sum of continuous segments): {total_duration_sec:.3f} seconds")
        print(f"[*] Number of Discontinuities: {jumps}")
        print(f"[*] Calculated Real Broadcast Bitrate: {avg_bitrate_bps/1000:.2f} kbps")
        print(f"[*] Calculated Real Broadcast Bitrate: {avg_bitrate_bps/1000000:.3f} Mbps")
    else:
        print("[ERROR] Could not calculate duration.")

if __name__ == "__main__":
    analyze_pcr_bitrate("/home/lmwang/sample/jaco/202508300200_Al-Taawoun_VS_Al-Nassr_2_cut_100M.ts")
