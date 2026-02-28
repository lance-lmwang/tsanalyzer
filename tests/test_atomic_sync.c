#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tsa.h"

void test_bitrate_atomic_sync() {
    printf("Testing Bitrate Atomic Sync...\n");

    tsa_config_t cfg = {0};
    cfg.is_live = true;
    tsa_handle_t* h = tsa_create(&cfg);
    assert(h != NULL);

    // Initial state: Send 1000 packets of PID 0x100 and 500 packets of PID 0x1FFF (null)
    uint8_t pkt_100[188], pkt_null[188];
    memset(pkt_100, 0, 188);
    pkt_100[0] = 0x47;
    pkt_100[1] = 0x01;
    pkt_100[3] = 0x10;
    memset(pkt_null, 0, 188);
    pkt_null[0] = 0x47;
    pkt_null[1] = 0x1F;
    pkt_null[2] = 0xFF;
    pkt_null[3] = 0x10;

    uint64_t now = 1000000000ULL;  // Start at 1s
    for (int i = 0; i < 1000; i++) {
        tsa_process_packet(h, pkt_100, now);
        now += 1000000;  // 1ms
    }
    for (int i = 0; i < 500; i++) {
        tsa_process_packet(h, pkt_null, now);
        now += 1000000;  // 1ms
    }

    // Trigger commit
    tsa_commit_snapshot(h, now);

    tsa_snapshot_full_t snap;
    tsa_take_snapshot_full(h, &snap);

    uint64_t physical_br = snap.stats.physical_bitrate_bps;
    uint64_t sum_pids_br = 0;
    for (int i = 0; i < 8192; i++) {
        sum_pids_br += (uint64_t)(snap.pids[i].bitrate_q16_16 / 65536.0);
    }

    printf("Physical Bitrate: %lu bps\n", physical_br);
    printf("Sum PID Bitrates: %lu bps\n", sum_pids_br);

    if (physical_br > 0) {
        double diff_pct = fabs((double)physical_br - (double)sum_pids_br) * 100.0 / (double)physical_br;
        printf("Difference: %.4f%%\n", diff_pct);
        assert(diff_pct < 5.0);  // Allow some margin for rounding in this rough test
    } else {
        assert(sum_pids_br == 0);
    }

    tsa_destroy(h);
    printf("[PASS] Atomic sync verified.\n");
}

int main() {
    test_bitrate_atomic_sync();
    return 0;
}
