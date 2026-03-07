#!/usr/bin/env python3
import time
import sys
import argparse
import urllib.request
import json

# ASCII Colors
GREEN = "\033[32m"
RED = "\033[31m"
YELLOW = "\033[33m"
CYAN = "\033[36m"
RESET = "\033[0m"
BOLD = "\033[1m"
DIM = "\033[2m"

def get_snapshot(url):
    try:
        with urllib.request.urlopen(url) as response:
            return json.loads(response.read().decode('utf-8'))
    except Exception as e:
        return None

def render_dashboard(snap):
    if not snap:
        print(f"{RED}Error: Could not fetch snapshot from API{RESET}")
        return

    lock = "LOCKED" if snap['status']['signal_lock'] else "LOST"
    s_color = GREEN if snap['status']['signal_lock'] else RED

    score = snap['status']['master_health']
    score_c = GREEN if score > 90 else (YELLOW if score > 60 else RED)

    rtt = snap['tier1_link']['srt_rtt_ms']
    loss = snap['tier1_link']['srt_loss_p0']
    retr = snap['tier1_link']['srt_retransmit_pct']

    fps = snap['tier3_essence']['video_fps']
    gop = snap['tier3_essence']['gop_ms'] / 1000.0
    av = snap['tier3_essence']['av_sync_offset_ms']
    br = snap['tier3_essence']['total_bitrate_bps'] / 1000000.0

    rstn = snap['tier4_predictive']['rst_network_s']
    rste = snap['tier4_predictive']['rst_encoder_s']
    mdi = snap['tier4_predictive']['mdi_df_ms']

    rst_c = GREEN if rstn > 30 else (YELLOW if rstn > 10 else RED)
    f_color = GREEN if fps > 24 else RED
    a_color = GREEN if abs(av) < 50 else (YELLOW if abs(av) < 150 else RED)

    out = []
    out.append(f"{BOLD}{CYAN}+------------------------------------------------------------------------------+{RESET}")
    out.append(f"{BOLD}{CYAN}| TSANALYZER PRO NOC - CLI DASHBOARD v3.0 {RESET}{DIM}             [ UTC {time.strftime('%H:%M:%S')} ]{RESET}{BOLD}{CYAN} |{RESET}")
    out.append(f"{BOLD}{CYAN}+------------------------------------------------------------------------------+{RESET}")
    l_box = f"{s_color}* {lock}{RESET}"
    out.append(f"| {BOLD}TIER 1 - MASTER CONTROL CONSOLE (SIGNAL STATUS){RESET}                       |")
    out.append(f"| {l_box}    RTT: {rtt}ms    LOSS: {s_color}{loss}{RESET}    RETR: {retr}%    SCORE: {score_c}{score}{RESET} |")
    out.append(f"{DIM}+------------------------------------------------------------------------------+{RESET}")
    out.append(f"| {BOLD}TIER 2 - TRANSPORT & LINK INTEGRITY (SRT/MDI){RESET}                          |")
    out.append(f"| MDI-DF: {mdi:.1f}ms    RETRANSMIT TAX: {retr}%                                     |")
    out.append(f"{DIM}+------------------------------------------------------------------------------+{RESET}")
    out.append(f"| {BOLD}TIER 3 - ETR 290 P1 (CRITICAL COMPLIANCE){RESET}                                |")

    m_sync = GREEN + "OK" + RESET if snap['tier2_compliance']['p1']['sync_loss'] == 0 else RED + "FAIL" + RESET
    m_pat = GREEN + "OK" + RESET if snap['tier2_compliance']['p1']['pat_error'] == 0 else RED + "FAIL" + RESET
    m_pmt = GREEN + "OK" + RESET if snap['tier2_compliance']['p1']['pmt_error'] == 0 else RED + "FAIL" + RESET
    m_cc = GREEN + "OK" + RESET if snap['tier2_compliance']['p1']['cc_error'] == 0 else RED + "FAIL" + RESET

    out.append(f"| [ SYNC:{m_sync} ]  [ PAT:{m_pat} ]  [ PMT:{m_pmt} ]  [ CC:{m_cc} ]                          |")
    out.append(f"{DIM}+------------------------------------------------------------------------------+{RESET}")
    out.append(f"| {BOLD}TIER 6 - ESSENCE QUALITY & TEMPORAL STABILITY{RESET}                          |")
    out.append(f"| FPS: {f_color}{fps:.2f}{RESET}    GOP: {gop:.2f}s    AV: {a_color}{av}ms{RESET}    BR: {br:.2f}Mbps             |")
    out.append(f"{DIM}+------------------------------------------------------------------------------+{RESET}")
    out.append(f"| {BOLD}TIER 4/5 - PREDICTIVE & PAYLOAD DYNAMICS{RESET}                               |")
    out.append(f"| Net RST: {rst_c}{rstn:.1f}s{RESET}    Enc RST: {rste:.1f}s    MDI-MLR: {loss} pkts/s               |")
    out.append(f"{BOLD}{CYAN}+------------------------------------------------------------------------------+{RESET}")

    sys.stdout.write("\033[H" + "\n".join(out) + "\n")
    sys.stdout.flush()

def main():
    parser = argparse.ArgumentParser(description="TSA CLI Monitor")
    parser.add_argument("--url", default="http://localhost:8088/api/v1/snapshot", help="API Snapshot URL")
    args = parser.parse_args()

    print("\033[2J") # Clear screen
    try:
        while True:
            snap = get_snapshot(args.url)
            render_dashboard(snap)
            time.sleep(1)
    except KeyboardInterrupt:
        print("\nExiting...")

if __name__ == "__main__":
    main()
