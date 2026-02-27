#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tsa.h"

void test_snapshot_mechanism() {
    printf("Testing Double-Buffered Snapshot Mechanism...\n");

    tsa_config_t cfg = {.pcr_ema_alpha = 0.01, .entropy_window_packets = 500};
    tsa_handle_t* h = tsa_create(&cfg);
    assert(h != NULL);

    tsa_snapshot_full_t ts_stats;

    // Initially zero
    tsa_take_snapshot_full(h, &ts_stats);
    assert(ts_stats.stats.total_ts_packets == 0);

    // Commit current state (still zero)
    tsa_commit_snapshot(h, 1000000000ULL);

    tsa_take_snapshot_full(h, &ts_stats);
    assert(ts_stats.stats.total_ts_packets == 0);

    tsa_destroy(h);
}

int main() {
    test_snapshot_mechanism();
    printf("Snapshot tests passed!\n");
    return 0;
}
