#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tsa.h"
#include "tsa_internal.h"

void test_pcr_jitter_metrics() {
    printf("Running test_pcr_jitter_metrics...\n");
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

    uint64_t now_ns = 1000000000ULL;
    uint64_t pcr_base = 0;

    for (int i = 0; i < 100; i++) {
        int64_t noise = (rand() % 200) - 100;
        uint64_t pcr_val = pcr_base + (uint64_t)i * 2700000 + (noise * 27 / 1000);

        uint64_t b = pcr_val / 300;
        uint16_t e = pcr_val % 300;
        pkt[6] = (uint8_t)(b >> 25);
        pkt[7] = (uint8_t)(b >> 17);
        pkt[8] = (uint8_t)(b >> 9);
        pkt[9] = (uint8_t)(b >> 1);
        pkt[10] = (uint8_t)((b << 7) | (e >> 8) | 0x7E);
        pkt[11] = (uint8_t)e;

        tsa_process_packet(h, pkt, now_ns + (uint64_t)i * 100000000ULL);

        if (i == 99) {
            tsa_commit_snapshot(h, now_ns + (uint64_t)i * 100000000ULL);
        }
    }

    tsa_snapshot_full_t snap;
    tsa_take_snapshot_full(h, &snap);

    printf("PCR Peak Jitter: %ld ns\n", snap.stats.pcr_jitter_max_ns);
    printf("PCR RMS Jitter: %ld ns\n", snap.stats.pcr_jitter_rms_ns);
    printf("PCR Max Interval: %ld ms\n", snap.stats.pcr_repetition_max_ms);

    assert(snap.stats.pcr_jitter_rms_ns > 0);
    assert(snap.stats.pcr_jitter_max_ns >= snap.stats.pcr_jitter_rms_ns);
    assert(snap.stats.pcr_repetition_max_ms >= 100);  // We set it to 100ms in the loop

    tsa_destroy(h);
    printf("test_pcr_jitter_metrics passed.\n");
}

int main() {
    test_pcr_jitter_metrics();
    return 0;
}
