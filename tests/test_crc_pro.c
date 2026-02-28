#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tsa.h"
#include "tsa_internal.h"

void test_pat_crc_error() {
    printf("Running test_pat_crc_error...\n");
    tsa_config_t cfg = {0};
    tsa_handle_t* h = tsa_create(&cfg);

    uint8_t pat_section[] = {0x00, 0xb0, 0x0d, 0x00, 0x01, 0xc1, 0x00,
                             0x00, 0x00, 0x01, 0xe1, 0x00, 0x00, 0x00,
                             0x00, 0x00};
    uint32_t crc = mpegts_crc32(pat_section, 12);
    pat_section[12] = (crc >> 24) & 0xFF;
    pat_section[13] = (crc >> 16) & 0xFF;
    pat_section[14] = (crc >> 8) & 0xFF;
    pat_section[15] = crc & 0xFF;

    uint8_t pkt[188] = {0};
    pkt[0] = 0x47;
    pkt[1] = 0x40;
    pkt[2] = 0x00;
    pkt[3] = 0x10;
    memcpy(pkt + 5, pat_section, 16);

    uint64_t now = 1000000000ULL;
    tsa_process_packet(h, pkt, now);
    assert(h->live.crc_error.count == 0);
    assert(h->seen_pat == true);

    pkt[10] ^= 0xFF;
    tsa_process_packet(h, pkt, now + 1000000);
    assert(h->live.crc_error.count == 1);

    tsa_destroy(h);
    printf("test_pat_crc_error passed.\n");
}

int main() {
    test_pat_crc_error();
    return 0;
}
