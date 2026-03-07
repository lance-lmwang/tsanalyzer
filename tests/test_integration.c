#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tsa.h"

void test_full_pipeline() {
    printf("Testing Passive Analysis Pipeline...\n");
    tsa_config_t cfg = {.analysis.pcr_ema_alpha = 0.01};
    tsa_handle_t* h = tsa_create(&cfg);
    assert(h != NULL);

    // Mock a valid TS packet (Sync=0x47)
    uint8_t pkt[188] = {0x47, 0x00, 0x00, 0x10, 0xff};
    tsa_process_packet(h, pkt, 1000000);

    tsa_commit_snapshot(h, 1000000000ULL);
    tsa_snapshot_full_t stats;
    assert(tsa_take_snapshot_full(h, &stats) == 0);
    assert(stats.stats.total_ts_packets == 1);

    tsa_destroy(h);
}

int main() {
    test_full_pipeline();
    printf("Integration test passed!\n");
    return 0;
}
