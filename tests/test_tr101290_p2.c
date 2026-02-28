#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "tsa.h"
#include "tsa_internal.h"

void encode_pcr(uint8_t* pkt, uint64_t pcr_ticks) {
    uint64_t base = pcr_ticks / 300;
    uint16_t ext = (uint16_t)(pcr_ticks % 300);
    pkt[3] |= 0x20;
    pkt[4] = 7;
    pkt[5] |= 0x10;
    pkt[6] = (uint8_t)(base >> 25);
    pkt[7] = (uint8_t)(base >> 17);
    pkt[8] = (uint8_t)(base >> 9);
    pkt[9] = (uint8_t)(base >> 1);
    pkt[10] = (uint8_t)((base << 7) | 0x7E | (ext >> 8));
    pkt[11] = (uint8_t)(ext & 0xFF);
}

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
    pkt[3] = 0x10;

    // First PCR
    uint64_t pcr = 1000000;
    encode_pcr(pkt, pcr);
    tsa_process_packet(h, pkt, 1000000000ULL);

    // Second PCR after 50ms (Limit is 40ms)
    pcr += (uint64_t)(50 * 27000);
    encode_pcr(pkt, pcr);
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
