#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tsa.h"
#include "tsa_internal.h"

void test_stc_jitter_stability() {
    printf("Running test_stc_jitter_stability...\n");
    tsa_config_t cfg = {0};
    tsa_handle_t* h = tsa_create(&cfg);
    assert(h != NULL);

    uint8_t pkt[188] = {0};
    pkt[0] = TS_SYNC_BYTE;
    pkt[1] = 0x01;  // PID 0x100
    pkt[2] = 0x00;
    pkt[3] = TS_AF_FLAG | TS_PAYLOAD_FLAG;
    pkt[4] = 7;
    pkt[5] = TS_PCR_FLAG;

    uint64_t start_ns = 1000000000ULL;
    uint64_t pcr_base = 0;

    // 1. Achive initial lock (Clean stream, 10s duration)
    for (int i = 0; i < 100; i++) {
        uint64_t pcr_val = pcr_base + (uint64_t)i * 2700000;  // 100ms
        uint64_t b = pcr_val / 300;
        uint16_t e = pcr_val % 300;
        pkt[6] = (uint8_t)(b >> 25);
        pkt[7] = (uint8_t)(b >> 17);
        pkt[8] = (uint8_t)(b >> 9);
        pkt[9] = (uint8_t)(b >> 1);
        pkt[10] = (uint8_t)((b << 7) | (e >> 8) | 0x7E);
        pkt[11] = (uint8_t)e;

        uint64_t now_ns = start_ns + (uint64_t)i * 100000000ULL;
        tsa_process_packet(h, pkt, now_ns);

        // Commit snapshot every 1 second
        if (i > 0 && i % 10 == 0) {
            tsa_commit_snapshot(h, now_ns);
        }
    }

    tsa_snapshot_full_t snap;
    tsa_take_snapshot_full(h, &snap);
    printf("Initial Lock: %s, Slope: %.4f\n", snap.predictive.stc_locked_bool ? "Yes" : "No",
           snap.predictive.stc_drift_slope);

    // RED PHASE: If this fails, even the base lock is broken.
    assert(snap.predictive.stc_locked_bool == true);

    // 2. Introduce Heavy Jitter (+/- 25ms random)
    printf("Introducing heavy jitter...\n");
    for (int i = 100; i < 200; i++) {
        uint64_t pcr_val = pcr_base + (uint64_t)i * 2700000;
        uint64_t b = pcr_val / 300;
        uint16_t e = pcr_val % 300;
        pkt[6] = (uint8_t)(b >> 25);
        pkt[7] = (uint8_t)(b >> 17);
        pkt[8] = (uint8_t)(b >> 9);
        pkt[9] = (uint8_t)(b >> 1);
        pkt[10] = (uint8_t)((b << 7) | (e >> 8) | 0x7E);
        pkt[11] = (uint8_t)e;

        int64_t jitter_ns = (rand() % 50000000) - 25000000;
        uint64_t now_ns = start_ns + (uint64_t)i * 100000000ULL + jitter_ns;
        tsa_process_packet(h, pkt, now_ns);

        if (i % 10 == 0) {
            tsa_commit_snapshot(h, now_ns);
            tsa_take_snapshot_full(h, &snap);
            printf("Lock Status under heavy jitter: %s, Slope: %.4f\n", snap.predictive.stc_locked_bool ? "Yes" : "No",
                   snap.predictive.stc_drift_slope);
        }
    }

    tsa_take_snapshot_full(h, &snap);
    printf("Final status after heavy jitter: %s\n", snap.predictive.stc_locked_bool ? "Locked" : "Unlocked");

    // RED PHASE: Expect lock to drop under 25ms jitter
    assert(snap.predictive.stc_locked_bool == false);

    // 3. Remove Jitter and check recovery
    printf("Removing jitter for recovery...\n");
    for (int i = 200; i < 300; i++) {
        uint64_t pcr_val = pcr_base + (uint64_t)i * 2700000;
        uint64_t b = pcr_val / 300;
        uint16_t e = pcr_val % 300;
        pkt[6] = (uint8_t)(b >> 25);
        pkt[7] = (uint8_t)(b >> 17);
        pkt[8] = (uint8_t)(b >> 9);
        pkt[9] = (uint8_t)(b >> 1);
        pkt[10] = (uint8_t)((b << 7) | (e >> 8) | 0x7E);
        pkt[11] = (uint8_t)e;

        uint64_t now_ns = start_ns + (uint64_t)i * 100000000ULL;
        tsa_process_packet(h, pkt, now_ns);
        if (i % 10 == 0) tsa_commit_snapshot(h, now_ns);
    }

    tsa_take_snapshot_full(h, &snap);
    printf("Status after recovery: %s\n", snap.predictive.stc_locked_bool ? "Locked" : "Unlocked");
    assert(snap.predictive.stc_locked_bool == true);

    tsa_destroy(h);
    printf("test_stc_jitter_stability passed.\n");
}

int main() {
    test_stc_jitter_stability();
    return 0;
}
