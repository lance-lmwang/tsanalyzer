#include <assert.h>
#include <math.h>
#include <stdio.h>

#include "tsa.h"
#include "tsa_internal.h"

int main() {
    tsa_config_t cfg = {0};
    cfg.is_live = true;
    tsa_handle_t* h = tsa_create(&cfg);

    // 1. Verify Network RST
    uint64_t pcr_bitrate = 10000000;
    uint64_t pkts = 1000;
    uint64_t ds_ns = 188000000;
    h->live->total_ts_packets = pkts;
    h->prev_snap_base->total_ts_packets = 0;
    h->last_snap_ns = h->start_ns;
    h->live->pcr_bitrate_bps = pcr_bitrate;
    h->live->pcr_jitter_max_ns = 10000000;  // 10ms

    tsa_commit_snapshot(h, h->start_ns + ds_ns);
    tsa_snapshot_full_t snap;
    tsa_take_snapshot_full(h, &snap);
    printf("RST Net: %.3fs\n", snap.predictive.rst_network_s);
    assert(snap.predictive.rst_network_s > 0.19 && snap.predictive.rst_network_s < 0.21);

    // 2. Verify Encoder RST (Drift)
    // Max Drift = 100ms = 100,000,000 ns
    // Current Drift = 10ms = 10,000,000 ns
    // Drift Rate = 1000ppm = 0.001 ns/ns
    // Remaining Drift = 90ms
    // RST = 0.09 / 0.001 = 90s
    h->live->pcr_accuracy_ns = 10000000;
    h->stc_slope_q64 = TO_Q64_64(1.001);  // 1000ppm too fast

    tsa_commit_snapshot(h, h->start_ns + ds_ns * 2);
    tsa_take_snapshot_full(h, &snap);
    printf("RST Enc (Drift): %.3fs\n", snap.predictive.rst_encoder_s);
    assert(snap.predictive.rst_encoder_s > 89.0 && snap.predictive.rst_encoder_s < 91.0);

    printf("RST prediction verified.\n");

    tsa_destroy(h);
    return 0;
}
