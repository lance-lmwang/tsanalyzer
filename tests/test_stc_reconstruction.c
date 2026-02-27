#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tsa.h"
#include "tsa_internal.h"

void test_stc_lock() {
    printf("Running test_stc_lock...\n");
    tsa_config_t cfg = {0};
    tsa_handle_t* h = tsa_create(&cfg);
    assert(h != NULL);

    // Create a dummy TS packet with PCR
    uint8_t pkt[188] = {0};
    pkt[0] = TS_SYNC_BYTE;
    pkt[1] = 0x01;  // PID 0x100
    pkt[2] = 0x00;
    pkt[3] = TS_AF_FLAG | TS_PAYLOAD_FLAG;
    pkt[4] = 7;  // AF length
    pkt[5] = TS_PCR_FLAG;

    // We need to send enough PCRs to achieve lock (spec says 50 samples + 5 cycles)
    // 100ms interval * 100 = 10 seconds
    uint64_t now_ns = 1000000000ULL;
    uint64_t pcr_base = 0;

    for (int i = 0; i < 100; i++) {
        uint64_t pcr_val = pcr_base + (uint64_t)i * 2700000;
        uint64_t b = pcr_val / 300;
        uint16_t e = pcr_val % 300;

        pkt[6] = (uint8_t)(b >> 25);
        pkt[7] = (uint8_t)(b >> 17);
        pkt[8] = (uint8_t)(b >> 9);
        pkt[9] = (uint8_t)(b >> 1);
        pkt[10] = (uint8_t)((b << 7) | (e >> 8) | 0x7E);
        pkt[11] = (uint8_t)e;

        tsa_process_packet(h, pkt, now_ns + (uint64_t)i * 100000000ULL);

        if (i % 10 == 0) {
            tsa_commit_snapshot(h, now_ns + (uint64_t)i * 100000000ULL);
        }
    }

    tsa_snapshot_full_t snap;
    tsa_take_snapshot_full(h, &snap);

    printf("STC Locked: %s, Slope: %.4f\n", snap.predictive.stc_locked_bool ? "Yes" : "No",
           snap.predictive.stc_drift_slope);

    assert(snap.predictive.stc_locked_bool == true);

    tsa_destroy(h);
    printf("test_stc_lock passed.\n");
}

int main() {
    test_stc_lock();
    return 0;
}
