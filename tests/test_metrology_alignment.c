#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "tsa.h"
#include "tsa_internal.h"

/**
 * @brief Regression test to ensure metrology parameters are correctly aligned
 * in the snapshot and that errors are properly aggregated from tracks.
 */
int main() {
    printf(">>> STARTING METROLOGY ALIGNMENT REGRESSION TEST <<<\n");

    tsa_config_t cfg = {0};
    cfg.op_mode = TSA_MODE_REPLAY;
    tsa_handle_t* h = tsa_create(&cfg);
    assert(h != NULL);

    /* 1. Verify Bitrate Alignment: pcr_bitrate_bps MUST match physical_bitrate_bps */
    h->live->total_ts_packets = 1000000;
    h->live->physical_bitrate_bps = 38000000;

    /* Mock some time passing to allow calculation */
    h->phys_stats.window_start_ns = 1000000000ULL;
    h->phys_stats.last_snap_bytes = 500000;

    tsa_commit_snapshot(h, 2000000000ULL);

    tsa_snapshot_full_t snap;
    tsa_take_snapshot_full(h, &snap);

    printf("Check Bitrate Alignment: Phys=%lu, PCR=%lu\n", (unsigned long)snap.stats.physical_bitrate_bps,
           (unsigned long)snap.stats.pcr_bitrate_bps);

    assert(snap.stats.physical_bitrate_bps == 38000000);
    assert(snap.stats.pcr_bitrate_bps == 38000000);
    assert(snap.summary.pcr_bitrate_bps == snap.summary.physical_bitrate_bps);

    /* 2. Verify PCR Error Aggregation: P1.1 errors from tracks must reach global stats */
    uint16_t test_pid = 0x100;
    h->pcr_tracks[test_pid].initialized = true;
    h->pcr_tracks[test_pid].priority_1_errors = 42;

    /* Second commit to trigger aggregation and snapshot update */
    tsa_commit_snapshot(h, 3000000000ULL);
    tsa_take_snapshot_full(h, &snap);

    printf("Check PCR Error Aggregation: Track[0x%04x]=42, Global=%lu\n", test_pid,
           (unsigned long)snap.stats.pcr_repetition_error.count);

    assert(snap.stats.pcr_repetition_error.count == 42);
    assert(snap.stats.alarm_pcr_repetition_error == true);

    /* 3. Verify Multi-PID Aggregation */
    h->pcr_tracks[0x200].initialized = true;
    h->pcr_tracks[0x200].priority_1_errors = 10;

    tsa_commit_snapshot(h, 4000000000ULL);
    tsa_take_snapshot_full(h, &snap);

    printf("Check Multi-PID Aggregation: Total Expected=52, Global=%lu\n",
           (unsigned long)snap.stats.pcr_repetition_error.count);
    assert(snap.stats.pcr_repetition_error.count == 52);

    tsa_destroy(h);
    printf(">>> METROLOGY ALIGNMENT REGRESSION TEST PASSED <<<\n");
    return 0;
}
