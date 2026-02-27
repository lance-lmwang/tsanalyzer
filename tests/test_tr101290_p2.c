#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "tsa.h"
#include "tsa_internal.h"

void test_p2_pcr_repetition() {
    printf("Running test_p2_pcr_repetition...\n");
    tsa_config_t cfg = {0};
    tsa_handle_t* h = tsa_create(&cfg);
    assert(h != NULL);

    uint8_t pkt[188];
    memset(pkt, 0, 188);
    pkt[0] = 0x47;
    pkt[1] = 0x01;  // PID 0x0100
    pkt[2] = 0x00;
    pkt[3] = 0x30;  // AF + Payload
    pkt[4] = 7;     // AF length
    pkt[5] = 0x10;  // PCR flag

    // First PCR
    uint64_t pcr = 1000000;
    pkt[6] = (pcr >> 25) & 0xFF;
    pkt[7] = (pcr >> 17) & 0xFF;
    pkt[8] = (pcr >> 9) & 0xFF;
    pkt[9] = (pcr >> 1) & 0xFF;
    pkt[10] = (pcr << 7) & 0x80;
    pkt[11] = 0;

    tsa_process_packet(h, pkt, 1000000000ULL);

    // Second PCR after 50ms (Limit is 40ms)
    pcr += (uint64_t)(50 * 27000);
    pkt[6] = (pcr >> 25) & 0xFF;
    pkt[7] = (pcr >> 17) & 0xFF;
    pkt[8] = (pcr >> 9) & 0xFF;
    pkt[9] = (pcr >> 1) & 0xFF;
    pkt[10] = (pcr << 7) & 0x80;

    tsa_process_packet(h, pkt, 1050000000ULL);

    tsa_commit_snapshot(h, 1060000000ULL);
    tsa_snapshot_full_t s;
    tsa_take_snapshot_full(h, &s);

    assert(s.stats.pcr_repetition_error.count > 0);

    tsa_destroy(h);
    printf("test_p2_pcr_repetition passed.\n");
}

int main() {
    test_p2_pcr_repetition();
    return 0;
}
