#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tsa.h"

void test_bitrate_normalization() {
    printf("Testing Bitrate Normalization...\n");

    tsa_config_t cfg = {0};
    cfg.op_mode = TSA_MODE_LIVE;
    tsa_handle_t* h = tsa_create(&cfg);
    assert(h != NULL);

    // Baseline
    uint8_t pkt[188] = {0x47, 0x01, 0x00, 0x10};
    uint64_t now = 1000000000ULL;
    for (int i = 0; i < 100; i++) {
        tsa_process_packet(h, pkt, now);
        now += 1000000;
    }
    tsa_commit_snapshot(h, now);

    // Mock high PCR rate vs low physical rate
    // Physical: 10 packets in 10ms = 10 * 188 * 8 / 0.01 = 150,400 bps
    // PCR: 10 packets with PCRs advancing by 100ms each = 100ms interval = very fast clock? No.
    // Let's just say PCR says 10Mbps but we deliver at 1Mbps physically.

    for (int i = 0; i < 10; i++) {
        uint8_t pcr_pkt[188];
        memset(pcr_pkt, 0, 188);
        pcr_pkt[0] = 0x47;
        pcr_pkt[1] = 0x01;
        pcr_pkt[3] = 0x30;
        pcr_pkt[4] = 7;
        pcr_pkt[5] = 0x10;

        // PCR base incrementing much faster than 'now'
        uint64_t pcr_val_ns = 2000000000ULL + (i * 100000);  // 100us per packet = 15Mbps
        uint64_t t = (pcr_val_ns * 27) / 1000;
        uint64_t base = t / 300;
        pcr_pkt[6] = (base >> 25) & 0xFF;
        pcr_pkt[7] = (base >> 17) & 0xFF;
        pcr_pkt[8] = (base >> 9) & 0xFF;
        pcr_pkt[9] = (base >> 1) & 0xFF;
        pcr_pkt[10] = ((base & 0x01) << 7) | 0x7E;

        tsa_process_packet(h, pcr_pkt, now);
        now += 1000000;  // 1ms real time = 1.5Mbps physical
    }

    tsa_commit_snapshot(h, now);

    tsa_snapshot_full_t snap;
    tsa_take_snapshot_full(h, &snap);

    uint64_t phys = snap.stats.physical_bitrate_bps;
    uint64_t sum = 0;
    for (uint32_t i = 0; i < snap.active_pid_count; i++) {
        sum += (uint64_t)((double)snap.pids[i].bitrate_q16_16 / 65536.0);
    }

    printf("Physical: %lu, Sum PID: %lu\n", phys, sum);

    if (phys > 0) {
        double diff = (double)sum - (double)phys;
        double diff_pct = diff * 100.0 / (double)phys;
        printf("Difference: %.4f%%\n", diff_pct);
        // Sum should NOT exceed Physical by more than 5% due to normalization
        assert(diff_pct < 5.0);
    }

    tsa_destroy(h);
    printf("[PASS] Normalization verified.\n");
}

int main() {
    test_bitrate_normalization();
    return 0;
}
