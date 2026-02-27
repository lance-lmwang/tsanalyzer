#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "tsa.h"
#include "tsa_internal.h"

void test_rst_network_depletion() {
    printf("Running test_rst_network_depletion...\n");
    tsa_config_t cfg = {0};
    cfg.is_live = true;
    tsa_handle_t* h = tsa_create(&cfg);
    assert(h != NULL);

    // To get 8Mbps physical with 1s ds:
    uint64_t physical_bitrate = 8000000;
    uint64_t pkts = physical_bitrate / (188 * 8);
    h->live.total_ts_packets = pkts;
    h->prev_snap_base.total_ts_packets = 0;
    h->last_snap_ns = h->start_ns;
    
    h->live.pcr_bitrate_bps = 10000000;
    h->live.pcr_jitter_max_ns = 10000000; // 10ms jitter
    h->srt_live.effective_rcv_latency_ms = 50; // 40ms margin
    
    tsa_commit_snapshot(h, h->start_ns + 1000000000ULL);

    tsa_snapshot_full_t s1;
    tsa_take_snapshot_full(h, &s1);
    printf("Network RST: %.3f s (MDI-DF: %.2f ms, Phys: %lu)\n", 
           s1.predictive.rst_network_s, s1.stats.mdi_df_ms, s1.stats.physical_bitrate_bps);

    // Margin = 40ms. Bits = 400,000. Depletion = 2,000,000. RST = 0.2s.
    assert(s1.predictive.rst_network_s > 0.19 && s1.predictive.rst_network_s < 0.21);

    tsa_destroy(h);
    printf("test_rst_network_depletion passed.\n");
}

int main() {
    test_rst_network_depletion();
    return 0;
}
