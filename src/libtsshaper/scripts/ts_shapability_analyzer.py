import sys
import re

def analyze_log(filepath, muxrate_kbps, muxdelay_s):
    in_bps_list = []
    with open(filepath, 'r') as f:
        for line in f:
            if "[T-STD SEC]" in line:
                match = re.search(r'In:\s*(\d+)k', line)
                if match:
                    in_bps_list.append(int(match.group(1)))

    if not in_bps_list:
        print(f"Result: No data found for analysis.")
        return

    in_bps_list = in_bps_list[4:]  # Drop first 4s warmup
    if not in_bps_list:
        return

    in_bps_sorted = sorted(in_bps_list)
    n = len(in_bps_sorted)
    p50 = in_bps_sorted[int(n * 0.50)]
    p95 = in_bps_sorted[int(n * 0.95)]
    p99 = in_bps_sorted[int(n * 0.99)]
    max_val = in_bps_sorted[-1]

    # ❗问题 1: Use max for upper bound fallback, but discounted
    burst_est = max(p99 - p50, (max_val - p50) * 0.7)

    # ❗问题 3: Calculate burst duration (consecutive windows > P95)
    max_consecutive = 0
    current_consecutive = 0
    for val in in_bps_list:
        if val > p95:
            current_consecutive += 1
            max_consecutive = max(max_consecutive, current_consecutive)
        else:
            current_consecutive = 0

    burst_len = max(1, max_consecutive) # At least 1 window

    # Burst Volume (Est Amplitude * Duration in seconds)
    # Assuming 1 window ~ 1 second for simplicity of this metric
    burst_volume = burst_est * burst_len

    # ❗问题 2: 15% safety loss for fragmentation and PCR jitter
    buffer_cap_raw = muxrate_kbps * muxdelay_s
    buffer_cap = buffer_cap_raw * 0.85

    safety = buffer_cap - burst_volume

    # Recommendation Logic
    if safety > 300:
        mode = "HARD_CLAMP (±4%)"
        status = "[SAFE] Safe to enforce strict limits"
    elif safety > 50:
        mode = "SOFT_CLAMP (±4% + buffer feedback)"
        status = "[WARN] Edge case, minor adaptations required"
    else:
        mode = "DYNAMIC (allow burst escape)"
        status = "[EMERGENCY] High Overflow Risk! Must open valves"

    # Minimum Muxdelay Required
    rec_muxdelay = (burst_volume / (muxrate_kbps * 0.85)) * 1.2

    print(f"--- TS Shapability Analyzer (Control Theory Model) ---")
    print(f"[Physical Constraints]")
    print(f"  Muxrate        : {muxrate_kbps} kbps")
    print(f"  Muxdelay       : {muxdelay_s:.1f} s")
    print(f"  Usable Buffer  : {buffer_cap:.1f} kbits (15% reserved for jitter/frag)")
    print(f"")
    print(f"[Video Flow Characteristics]")
    print(f"  Baseline (P50) : {p50} kbps")
    print(f"  P99 / Max      : {p99} / {max_val} kbps")
    print(f"  Burst Est (Amp): {burst_est:.1f} kbps")
    print(f"  Burst Duration : {burst_len} windows (~{burst_len}s)")
    print(f"  Burst Volume   : {burst_volume:.1f} kbits")
    print(f"")
    print(f"[System Safety & Strategy]")
    print(f"  Safety Margin  : {safety:.1f} kbits")
    print(f"  Recommended    : {mode}")
    print(f"  Risk Level     : {status}")
    print(f"  Min Muxdelay   : {rec_muxdelay:.2f} s (required to achieve ±4% without overflow)")
    print(f"")

if __name__ == "__main__":
    if len(sys.argv) < 4:
        print("Usage: tsa_shapability_analyzer.py <log_file> <muxrate_kbps> <muxdelay_s>")
        sys.exit(1)

    log_file = sys.argv[1]
    muxrate = int(sys.argv[2].replace('k', ''))
    muxdelay = float(sys.argv[3])

    analyze_log(log_file, muxrate, muxdelay)
