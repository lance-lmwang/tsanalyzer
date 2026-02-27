#include <assert.h>
#include <stdio.h>
#include <stdbool.h>
#include "tsa.h"

int main() {
    tsa_snapshot_full_t snapshot;
    
    // Tier 1: Link Survival
    // signal_lock should be in summary
    (void)snapshot.summary.signal_lock;
    
    // SRT stats should be available
    (void)snapshot.srt.rtt_ms;
    (void)snapshot.srt.arq_retransmission_rate;
    (void)snapshot.srt.pkt_rcv_loss_total; // For Unrecovered Loss (P0)
    
    // Tier 2: Hybrid Matrix
    (void)snapshot.srt.link_bandwidth_mbps;
    (void)snapshot.srt.pkt_rcv_nak_total;
    (void)snapshot.srt.rcv_buf_ms; // Buffer Margin
    
    // Tier 3: ES Vitals
    (void)snapshot.stats.video_fps;
    (void)snapshot.stats.gop_ms;
    (void)snapshot.stats.av_sync_ms;
    (void)snapshot.stats.physical_bitrate_bps;
    (void)snapshot.stats.pcr_bitrate_bps;

    printf("V3.5 Snapshot fields verified.\n");
    return 0;
}
