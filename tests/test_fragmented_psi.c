#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "tsa.h"
#include "tsa_internal.h"

void test_fragmented_pat() {
    tsa_config_t cfg = {0};
    tsa_handle_t* h = tsa_create(&cfg);
    assert(h != NULL);
    
    // PAT section: 20 bytes total (16 payload + 4 CRC)
    // Length field in header is section_length (bytes following the length field including CRC)
    // section_length = 20 - 3 = 17 (0x11)
    uint8_t full_pat[20] = {
        0x00, 0xB0, 0x11, 0x00, 0x01, 0xC1, 0x00, 0x00, 
        0x00, 0x01, 0xE1, 0x00, 0x00, 0x02, 0xE2, 0x00,
        0x00, 0x00, 0x00, 0x00 
    };
    uint32_t crc = mpegts_crc32(full_pat, 16);
    full_pat[16] = (crc >> 24) & 0xFF;
    full_pat[17] = (crc >> 16) & 0xFF;
    full_pat[18] = (crc >> 8) & 0xFF;
    full_pat[19] = crc & 0xFF;

    // Split across 3 packets: 7 bytes, 7 bytes, 6 bytes
    uint8_t pkt1[188] = {0}, pkt2[188] = {0}, pkt3[188] = {0};
    
    // Packet 1: PUSI set, payload starts with pointer_field=0
    pkt1[0] = 0x47; pkt1[1] = 0x40; pkt1[2] = 0x00; pkt1[3] = 0x11; 
    pkt1[4] = 0x00; // pointer_field
    memcpy(pkt1 + 5, full_pat, 7);

    // Packet 2: PUSI not set, continues payload
    pkt2[0] = 0x47; pkt2[1] = 0x00; pkt2[2] = 0x00; pkt2[3] = 0x12;
    memcpy(pkt2 + 4, full_pat + 7, 7);

    // Packet 3: PUSI not set, completes payload
    pkt3[0] = 0x47; pkt3[1] = 0x00; pkt3[2] = 0x00; pkt3[3] = 0x13;
    memcpy(pkt3 + 4, full_pat + 14, 6);

    ts_decode_result_t res;
    tsa_decode_packet(h, pkt1, 1000, &res);
    assert(h->pid_filters[0].active == true);
    assert(h->pid_filters[0].complete == false);

    tsa_decode_packet(h, pkt2, 2000, &res);
    assert(h->pid_filters[0].active == true);
    assert(h->pid_filters[0].complete == false);

    tsa_decode_packet(h, pkt3, 3000, &res);
    // After pkt3, the section should be complete and processed.
    // The filter should be reset to inactive.
    assert(h->pid_filters[0].active == false);
    assert(h->seen_pat == true);
    assert(h->program_count == 2);
    assert(h->programs[0].pmt_pid == 0x100);
    assert(h->programs[1].pmt_pid == 0x200);

    tsa_destroy(h);
    printf("test_fragmented_pat passed\n");
}

int main() {
    test_fragmented_pat();
    return 0;
}
