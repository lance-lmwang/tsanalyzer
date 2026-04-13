#!/usr/bin/env python3
"""
T-STD Telemetry Analyzer Pro
Broadcast-Grade Validation Tool (DVB-aligned)

Key Features:
- Multi-scale bitrate analysis (10/100/1000 ms)
- PCR jitter + interval validation
- VBV buffer occupancy modeling
- Token bucket stability
- NULL stuffing distribution analysis
- CI JSON report output
"""

import sys
import re
import json
import os
import argparse
import pandas as pd
import numpy as np

# ---------------------------
# 1. Parser
# ---------------------------

def parse_trace(filename: str) -> pd.DataFrame:
    if not os.path.exists(filename):
        print(f"[FATAL] Log file not found: {filename}")
        print(f"        Current working directory: {os.getcwd()}")
        sys.exit(1)

    pattern = re.compile(
        r"\[T-STD\]\s+"
        r"STC:(\d+)\s+"
        r"PID:(\d+)\s+"
        r"TOK:(-?\d+)\s+"
        r"GTOK:(-?\d+)\s+"
        r"BUF:(\d+)\s+"
        r"ACT:([^\s]+)\s+"
        r"ERR:(-?\d+)\s+"
        r"VIOL:(\d+)"
    )

    data = []

    print(f"[*] Parsing trace file: {filename} ...")
    with open(filename) as f:
        for line in f:
            m = pattern.search(line)
            if m:
                stc, pid, tok, gtok, buf, act, err, viol = m.groups()
                data.append({
                    "time": int(stc),
                    "pid": int(pid),
                    "tokens": int(tok),
                    "global_tokens": int(gtok),
                    "buffer": int(buf),
                    "action": act,
                    "pcr_err": int(err),
                    "violation": int(viol)
                })

    if not data:
        print("[FATAL] No valid telemetry data found in log")
        sys.exit(1)

    df = pd.DataFrame(data)
    print(f"[*] Parsed {len(df)} discrete T-STD events successfully.")
    return df


# ---------------------------
# 2. Bitrate Engine
# ---------------------------

def calc_window(df, window_ms):
    ticks = int(window_ms * 27000)
    df = df.copy()
    df["window"] = (df["time"] // ticks) * ticks
    # Group by window and count packets (188 bytes each)
    g = df.groupby("window").size().reset_index(name="pkts")
    g["bps"] = g["pkts"] * 188 * 8 * 1000 / window_ms
    return g


# ---------------------------
# 3. Gates
# ---------------------------

def check_pcr(df, report):
    pcr = df[df["action"].str.contains("PCR", na=False)]
    if pcr.empty:
        report["pcr"] = "no_data"
        return 0

    # Convert ticks (27MHz) to nanoseconds
    pcr_err_ns = pcr["pcr_err"] * 1000 / 27.0
    jitter_ns = pcr_err_ns.abs().max()
    p95_jitter_ns = pcr_err_ns.abs().quantile(0.95)

    # Calculate drift (ppm)
    pcr_times = pcr["time"]
    pcr_vals = pcr["time"] + pcr["pcr_err"] # Approximated actual PCR value
    if len(pcr_vals) > 10:
        elapsed_stc = pcr_times.iloc[-1] - pcr_times.iloc[0]
        elapsed_pcr = pcr_vals.iloc[-1] - pcr_vals.iloc[0]
        drift_ppm = (elapsed_pcr - elapsed_stc) * 1000000.0 / elapsed_stc if elapsed_stc != 0 else 0
    else:
        drift_ppm = 0

    interval = pcr["time"].diff() / 27000.0  # ms
    interval_max = interval.max()

    print(f"[*] PCR_jitter_ns:    max={jitter_ns:.1f}, p95={p95_jitter_ns:.1f}")
    print(f"[*] PCR_accuracy_ns:  {jitter_ns:.1f} (Limit: 500ns)")
    print(f"[*] PCR_drift_ppm:    {drift_ppm:.3f}")

    fail = 0
    if jitter_ns > 500: # TR 101 290 P1 limit
        print(f"[FAIL] PCR jitter {jitter_ns:.1f} ns exceeded broadcast limit (500ns)")
        fail += 1
    elif jitter_ns > 100: # Industrial strict limit
        print(f"[WARN] PCR jitter {jitter_ns:.1f} ns exceeds high-precision threshold (100ns)")
    else:
        print("[PASS] PCR jitter OK")

    if interval_max > 40:
        print(f"[FAIL] PCR interval {interval_max:.2f} ms (Limit: 40ms)")
        fail += 1
    else:
        print(f"[PASS] PCR interval OK (Max: {interval_max:.2f} ms)")

    report["pcr_metrics"] = {
        "max_jitter_ns": jitter_ns,
        "p95_jitter_ns": p95_jitter_ns,
        "drift_ppm": drift_ppm
    }
    report["pcr"] = "pass" if fail == 0 else "fail"
    return fail


def check_psi_interval(df, report):
    fail = 0
    psi_metrics = {}

    # 1. PAT Check (PID 0)
    pat = df[(df["pid"] == 0) & (df["action"] == "PSI")]
    if not pat.empty:
        pat_interval = pat["time"].diff() / 27000.0
        max_pat = pat_interval.max()
        psi_metrics["pat_max_ms"] = max_pat
        if max_pat > 500: # ETSI TR 101 290 P1: 500ms (Hard limit)
            print(f"[FAIL] PAT interval {max_pat:.2f} ms > 500ms limit (DVB Violation)")
            fail += 1
        elif max_pat > 150: # Industrial best practice is ~100ms, allow some jitter
            print(f"[WARN] PAT interval {max_pat:.2f} ms exceeds best-practice (100-150ms)")
        else:
            print(f"[PASS] PAT interval OK (Max: {max_pat:.2f} ms)")
    else:
        print("[FAIL] PAT (PID 0) missing!")
        fail += 1

    # 2. SDT Check (PID 0x11/17)
    sdt = df[(df["pid"] == 17) & (df["action"] == "PSI")]
    if not sdt.empty:
        sdt_interval = sdt["time"].diff() / 27000.0
        max_sdt = sdt_interval.max()
        psi_metrics["sdt_max_ms"] = max_sdt
        # DVB P1 limit is 2000ms, but industrial 'sdt_period' usually target 500ms
        if max_sdt > 2000:
            print(f"[FAIL] SDT interval {max_sdt:.2f} ms > 2000ms limit (sdt_period violation)")
            fail += 1
        elif max_sdt > 500:
            print(f"[WARN] SDT interval {max_sdt:.2f} ms exceeds high-speed threshold (500ms)")
        else:
            print(f"[PASS] SDT interval OK (Max: {max_sdt:.2f} ms)")
    else:
        # SDT is not strictly P1 but highly recommended for DVB
        print("[WARN] SDT (PID 0x11) missing from stream")

    # 3. PMT Check (PIDs that are not 0, 1, 16, 17, 18, 20... but marked as PSI)
    # Typically PMTs are between 0x0020 and 0x1FFE
    pmt = df[(df["pid"] > 0) & (df["pid"] != 17) & (df["action"] == "PSI")]
    if not pmt.empty:
        for pid, group in pmt.groupby("pid"):
            pmt_interval = group["time"].diff() / 27000.0
            max_pmt = pmt_interval.max()
            psi_metrics[f"pmt_0x{pid:04x}_max_ms"] = max_pmt
            if max_pmt > 400: # TR 101 290 P1: 400ms
                print(f"[FAIL] PMT (PID 0x{pid:04x}) interval {max_pmt:.2f} ms > 400ms limit")
                fail += 1
            else:
                print(f"[PASS] PMT (PID 0x{pid:04x}) interval OK (Max: {max_pmt:.2f} ms)")

    report["psi_metrics"] = psi_metrics
    report["psi"] = "pass" if fail == 0 else "fail"
    return fail


def check_bitrate(df, report):
    fail = 0
    metrics = {}

    for w in [1, 10, 100, 1000]:
        g = calc_window(df, w)
        if len(g) > 4:
            g = g.iloc[2:-2]

        if g.empty:
            continue

        stddev = g["bps"].std()
        dev_percent = (g["bps"] - g["bps"].mean()).abs().max() / g["bps"].mean() * 100 if g["bps"].mean() != 0 else 0

        metrics[f"window_{w}ms_stddev"] = stddev
        metrics[f"window_{w}ms_peak_dev_pct"] = dev_percent

        limit_pct = {
            1: 60.0,  # 1ms window: quantum granularity causes up to 51% dev at 2Mbps
            10: 10.0, # 10ms window: up to ~6% dev due to discrete packets
            100: 2.0,
            1000: 1.0
        }[w]

        print(f"[*] Bitrate {w}ms: stddev={stddev/1000:.1f} kbps, peak_dev={dev_percent:.2f}%")

        if dev_percent > limit_pct:
            print(f"[FAIL] CBR violation at {w}ms window (Dev {dev_percent:.2f}% > {limit_pct}%)")
            fail += 1
        else:
            print(f"[PASS] Bitrate {w}ms stable")

    report["bitrate_metrics"] = metrics
    report["bitrate"] = "pass" if fail == 0 else "fail"
    return fail


def check_vbv(df, report):
    fail = 0

    if df["violation"].sum() > 0:
        print("[FAIL] T-STD Hardware Violation Flag active!")
        fail += 1

    video_audio_df = df[~df["action"].isin(["NULL", "PSI", "PCR_ONLY"])]
    if not video_audio_df.empty:
        buf = video_audio_df["buffer"]
        min_buf = buf.min()
        max_buf = buf.max()

        # Check for underflow
        if min_buf <= 0:
            print(f"[FAIL] Buffer underflow detected (Min: {min_buf} bytes)")
            fail += 1
        else:
            print(f"[PASS] No buffer underflow (Min: {min_buf} bytes)")

        # Check for overflow (Simple heuristic if we don't know the exact limit, or look for max jump)
        # In T-STD, overflow is as bad as underflow.
        # If we see a suspicious plateau or flag, we mark it.
        # For now, we look for 'violation' flag which usually covers both.
        print(f"[*] VBV occupancy: min={min_buf}, max={max_buf} bytes")
    else:
        print("[PASS] No VBV data to check")

    report["vbv"] = "pass" if fail == 0 else "fail"
    return fail


def check_token(df, report):
    std = df["tokens"].std()
    # Token bucket should be extremely stable in a perfect CBR
    if std > 2000:
        print(f"[WARN] Token bucket jitter (std={std:.1f}) - possible micro-bursts")
        report["token"] = "warn"
    else:
        print(f"[PASS] Token bucket stable (std={std:.1f})")
        report["token"] = "pass"
    return 0


def check_null(df, report):
    null = df[df["action"] == "NULL"]

    if null.empty:
        report["null"] = "no_data"
        return 0

    intervals = null["time"].diff() / 27000.0 # ms
    stddev = intervals.std()
    max_gap = intervals.max()

    print(f"[*] NULL interval: stddev={stddev:.3f} ms, max_gap={max_gap:.3f} ms")

    if stddev > 2.0: # Highly irregular stuffing
        print(f"[WARN] NULL distribution irregular (stddev={stddev:.2f} ms)")
        report["null"] = "warn"
    else:
        print("[PASS] NULL distribution uniform")

    report["null_metrics"] = {"stddev_ms": stddev, "max_gap_ms": max_gap}
    report["null"] = "pass"
    return 0


# ---------------------------
# 4. CI Runner
# ---------------------------

def run(df, json_out=None):
    print("\n=== T-STD Analyzer Industrial ===\n")

    report = {}
    current_score = 100
    total_fails = 0

    total_fails += check_pcr(df, report)
    total_fails += check_psi_interval(df, report)
    total_fails += check_bitrate(df, report)
    total_fails += check_vbv(df, report)

    # These checks currently only set warning/pass in report, but we'll run them anyway
    check_token(df, report)
    check_null(df, report)

    # Deduction logic: 30 points per major failure category
    current_score -= (total_fails * 30)
    if current_score < 0:
        current_score = 0

    print("\n=== RESULT ===")
    print(f"Score: {current_score}/100")

    report["score"] = current_score

    if json_out:
        out_dir = os.path.dirname(json_out)
        if out_dir and not os.path.exists(out_dir):
            print(f"[WARN] Output directory does not exist: {out_dir}. Report will not be saved.")
        else:
            with open(json_out, "w") as f:
                json.dump(report, f, indent=2)
            print(f"[*] JSON report saved to: {json_out}")

    return 0 if total_fails == 0 else 1


# ---------------------------
# 5. Main
# ---------------------------

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="T-STD Telemetry Analyzer Pro")
    parser.add_argument("logfile", help="Path to the T-STD trace log file")
    parser.add_argument("--json", help="output json report file path")
    args = parser.parse_args()

    df = parse_trace(args.logfile)
    code = run(df, args.json)
    sys.exit(code)
