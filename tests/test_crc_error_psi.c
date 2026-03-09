#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "tsa.h"
#include "tsa_internal.h"

void test_crc_error_pat() {
    tsa_config_t cfg = {0};
    tsa_handle_t* h = tsa_create(&cfg);
    assert(h != NULL);

    // Valid PAT first (Version 0)
    uint8_t pat_v0[20] = {0x00, 0xB0, 0x11, 0x00, 0x01, 0xC1, 0x00, 0x00, 0x00, 0x01,
                          0xE1, 0x00, 0x00, 0x02, 0xE2, 0x00, 0x00, 0x00, 0x00, 0x00};
    uint32_t crc0 = mpegts_crc32(pat_v0, 16);
    pat_v0[16] = (crc0 >> 24) & 0xFF;
    pat_v0[17] = (crc0 >> 16) & 0xFF;
    pat_v0[18] = (crc0 >> 8) & 0xFF;
    pat_v0[19] = crc0 & 0xFF;

    uint8_t pkt[188] = {0};
    pkt[0] = 0x47;
    pkt[1] = 0x40;
    pkt[2] = 0x00;
    pkt[3] = 0x11;
    pkt[4] = 0x00;  // pointer_field
    memcpy(pkt + 5, pat_v0, 20);

    ts_decode_result_t res;
    tsa_decode_packet(h, pkt, 1000, &res);
    assert(h->seen_pat == true);
    assert(h->program_count == 2);
    assert(h->pid_filters[0].last_ver == 0);

    // Corrupted PAT (Version 1, but bad CRC)
    uint8_t pat_v1_bad[20] = {0x00, 0xB0, 0x11, 0x00, 0x01, 0xC3, 0x00, 0x00,  // 0xC3 is version 1
                              0x00, 0x03, 0xE3, 0x00, 0x00, 0x04, 0xE4, 0x00,
                              0xDE, 0xAD, 0xBE, 0xEF  // Intentional bad CRC
    };
    memcpy(pkt + 5, pat_v1_bad, 20);
    pkt[3] = 0x12;  // CC 2

    uint64_t prev_crc_errors __attribute__((unused)) = h->live->crc_error.count;
    tsa_decode_packet(h, pkt, 2000, &res);

    assert(h->live->crc_error.count == prev_crc_errors + 1);
    // Should NOT have updated programs
    assert(h->program_count == 2);
    assert(h->programs[0].pmt_pid == 0x100);
    assert(h->pid_filters[0].last_ver == 0);

    tsa_destroy(h);
    printf("test_crc_error_pat passed\n");
}

int main() {
    test_crc_error_pat();
    return 0;
}
