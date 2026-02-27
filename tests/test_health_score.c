#include <assert.h>
#include <stdio.h>
#include <math.h>
#include "tsa.h"
#include "tsa_internal.h"

int main() {
    tsa_config_t cfg = {0};
    cfg.is_live = true;
    tsa_handle_t* h = tsa_create(&cfg);

    // 1. Test Healthy State (Score should be 100)
    uint64_t bitrate = 10000000;
    uint64_t pkts = bitrate / (188 * 8);
    uint64_t now = h->start_ns + 1000000000ULL;
    h->live.total_ts_packets = pkts;
    h->prev_snap_base.total_ts_packets = 0;
    h->last_snap_ns = h->start_ns;
    h->live.physical_bitrate_bps = bitrate;
    h->live.pcr_bitrate_bps = bitrate;
    h->live.mdi_df_ms = 5.0;
    h->live.mdi_mlr_pkts_s = 0.0;
    h->stc_drift_slope = 1.0;
    h->seen_pat = true;
    h->seen_pmt = true;
    // Align PAT/PMT timestamps to NOW to avoid timeout penalty
    h->last_pat_ns = now;
    h->last_pmt_ns = now;

    tsa_commit_snapshot(h, now);
    tsa_snapshot_full_t snap;
    tsa_take_snapshot_full(h, &snap);
    printf("Score (Healthy): %.1f (Lid: %d, RST_Net: %.1f)\n",
           snap.predictive.master_health, snap.predictive.lid_active, snap.predictive.rst_network_s);
    assert(snap.predictive.master_health > 95.0);
    assert(!snap.predictive.lid_active);

    // 2. Test P1 Fatal Penalty (Active CC Error)
    // Score should be min(100 - 40, 60) = 60
    h->live.cc_error.count = 1;
    now += 1000000000ULL;
    h->last_pat_ns = now; // keep healthy
    h->last_pmt_ns = now;
    tsa_commit_snapshot(h, now);
    tsa_take_snapshot_full(h, &snap);
    printf("Score (CC Error): %.1f (Lid: %d)\n", snap.predictive.master_health, snap.predictive.lid_active);
    assert(snap.predictive.master_health <= 60.0);
    assert(snap.predictive.lid_active);

    // 3. Test Network RST Penalty (RST_Net = 8s)
    h->live.cc_error.count = 0;
    h->prev_snap_base.cc_error.count = 0;
    h->live.pcr_jitter_max_ns = 10000000; // 10ms jitter => 40ms margin
    h->live.pcr_bitrate_bps = 10000000;
    // 9.95M physical => 50k depletion => 0.4M / 50k = 8s RST
    h->live.total_ts_packets = h->prev_snap_base.total_ts_packets + (9950000 / (188 * 8));

    now += 1000000000ULL;
    h->last_pat_ns = now;
    h->last_pmt_ns = now;
    tsa_commit_snapshot(h, now);
    tsa_take_snapshot_full(h, &snap);
    printf("Score (RST Net %.1fs): %.1f (Lid: %d)\n",
           snap.predictive.rst_network_s, snap.predictive.master_health, snap.predictive.lid_active);
    assert(snap.predictive.master_health > 70.0 && snap.predictive.master_health < 80.0);
    assert(!snap.predictive.lid_active);

    printf("Health score logic verified.\n");

    tsa_destroy(h);
    return 0;
}
