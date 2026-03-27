#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "tsa.h"
#include "tsa_internal.h"

void test_pat_version_rollover() {
    tsa_config_t cfg = {0};
    tsa_handle_t* h = tsa_create(&cfg);
    assert(h != NULL);

    // PAT Version 0
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
    pkt[3] = 0x10;
    pkt[4] = 0x00;
    memcpy(pkt + 5, pat_v0, 20);

    tsa_process_packet(h, pkt, 1000);
    assert(h->seen_pat == true);
    assert(h->program_count == 2);
    assert(h->pid_filters[0] != NULL && h->pid_filters[0]->last_ver == 0);

    // PAT Version 1 (Update program list)
    uint8_t pat_v1[20] = {0x00, 0xB0, 0x11, 0x00, 0x01, 0xC3, 0x00, 0x00,  // 0xC3 is version 1
                          0x00, 0x03, 0xE3, 0x00, 0x00, 0x04, 0xE4, 0x00, 0x00, 0x00, 0x00, 0x00};
    uint32_t crc1 = mpegts_crc32(pat_v1, 16);
    pat_v1[16] = (crc1 >> 24) & 0xFF;
    pat_v1[17] = (crc1 >> 16) & 0xFF;
    pat_v1[18] = (crc1 >> 8) & 0xFF;
    pat_v1[19] = crc1 & 0xFF;

    pkt[3] = 0x11;  // CC 1
    memcpy(pkt + 5, pat_v1, 20);
    tsa_process_packet(h, pkt, 2000);

    assert(h->pid_filters[0] != NULL && h->pid_filters[0]->last_ver == 1);
    assert(h->program_count == 2);
    assert(h->programs[0].pmt_pid == 0x300);  // Should be updated
    assert(h->programs[1].pmt_pid == 0x400);

    // PAT Version 31 (Max version)
    uint8_t pat_v31[20] = {0x00, 0xB0, 0x11, 0x00, 0x01, 0xFF, 0x00, 0x00,  // 0xFF -> version 31 (11111)
                           0x00, 0x05, 0xE5, 0x00, 0x00, 0x06, 0xE6, 0x00, 0x00, 0x00, 0x00, 0x00};
    uint32_t crc31 = mpegts_crc32(pat_v31, 16);
    pat_v31[16] = (crc31 >> 24) & 0xFF;
    pat_v31[17] = (crc31 >> 16) & 0xFF;
    pat_v31[18] = (crc31 >> 8) & 0xFF;
    pat_v31[19] = crc31 & 0xFF;

    pkt[3] = 0x12;  // CC 2
    memcpy(pkt + 5, pat_v31, 20);
    tsa_process_packet(h, pkt, 3000);
    assert(h->pid_filters[0] != NULL && h->pid_filters[0]->last_ver == 31);
    assert(h->programs[0].pmt_pid == 0x500);

    // PAT Version 0 again (Rollover)
    pkt[3] = 0x13;  // CC 3
    memcpy(pkt + 5, pat_v0, 20);
    tsa_process_packet(h, pkt, 4000);
    assert(h->pid_filters[0] != NULL && h->pid_filters[0]->last_ver == 0);
    assert(h->programs[0].pmt_pid == 0x100);

    tsa_destroy(h);
    printf("test_pat_version_rollover passed\n");
}

int main() {
    test_pat_version_rollover();
    return 0;
}
