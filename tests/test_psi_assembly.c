#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "tsa.h"
#include "tsa_internal.h"

void test_pat_assembly() {
    tsa_config_t cfg = {0};
    tsa_handle_t* h = tsa_create(&cfg);
    assert(h != NULL);

    uint8_t pkt1[188] = {0};
    pkt1[0] = 0x47;
    pkt1[1] = 0x40; // PUSI, PID 0
    pkt1[2] = 0x00;
    pkt1[3] = 0x11; // Payload only, CC 1
    pkt1[4] = 0x00; // Pointer field
    
    // PAT: 0x00, 0xB0, 0x11, 0x00, 0x01, 0xC1, 0x00, 0x00, 0x00, 0x01, 0xE1, 0x00, 0x00, 0x02, 0xE2, 0x00, CRC(4)
    uint8_t full_pat[20] = {
        0x00, 0xB0, 0x11, 0x00, 0x01, 0xC1, 0x00, 0x00, 
        0x00, 0x01, 0xE1, 0x00, 0x00, 0x02, 0xE2, 0x00,
        0x00, 0x00, 0x00, 0x00 // CRC placeholder
    };
    uint32_t real_crc = mpegts_crc32(full_pat, 16);
    full_pat[16] = (real_crc >> 24) & 0xFF;
    full_pat[17] = (real_crc >> 16) & 0xFF;
    full_pat[18] = (real_crc >> 8) & 0xFF;
    full_pat[19] = real_crc & 0xFF;

    // Send first 10 bytes
    memcpy(pkt1 + 5, full_pat, 10);
    ts_decode_result_t res1;
    tsa_decode_packet(h, pkt1, 1000, &res1);
    
    assert(h->pid_filters[0].active == true);
    assert(h->pid_filters[0].complete == false);
    assert(h->program_count == 0);

    uint8_t pkt2[188] = {0};
    pkt2[0] = 0x47;
    pkt2[1] = 0x00; // PID 0
    pkt2[2] = 0x00;
    pkt2[3] = 0x12; // Payload only, CC 2
    
    // Remaining 10 bytes
    memcpy(pkt2 + 4, full_pat + 10, 10);
    ts_decode_result_t res2;
    tsa_decode_packet(h, pkt2, 2000, &res2);

    assert(h->seen_pat == true);
    assert(h->program_count == 2);
    assert(h->programs[0].pmt_pid == 0x100);
    assert(h->programs[1].pmt_pid == 0x200);

    tsa_destroy(h);
    printf("test_pat_assembly passed\n");
}

void test_sdt_assembly() {
    tsa_config_t cfg = {0};
    tsa_handle_t* h = tsa_create(&cfg);
    
    uint8_t sdt[] = {
        0x42, 0xF0, 0x25, // table_id (SDT actual), len=37
        0x00, 0x01, // ts_id
        0xC1, 0x00, 0x00, // version 0, etc
        0x00, 0x01, // original_net_id
        0xFF, // reserved
        // Service loop
        0x00, 0x01, // service_id
        0xFC, // EIT schedule=0, present/following=0, running=0, free_ca=0
        0x80, 0x19, // descriptors_loop_len=25
        0x48, 0x17, // service_descriptor, len=23
        0x01, // service_type (digital TV)
        0x08, 'P', 'r', 'o', 'v', 'i', 'd', 'e', 'r',
        0x0C, 'M', 'y', ' ', 'S', 'e', 'r', 'v', 'i', 'c', 'e', ' ', '1',
        0x00, 0x00, 0x00, 0x00 // CRC
    };
    uint32_t crc = mpegts_crc32(sdt, sizeof(sdt) - 4);
    sdt[sizeof(sdt)-4] = (crc >> 24) & 0xFF;
    sdt[sizeof(sdt)-3] = (crc >> 16) & 0xFF;
    sdt[sizeof(sdt)-2] = (crc >> 8) & 0xFF;
    sdt[sizeof(sdt)-1] = crc & 0xFF;

    uint8_t pkt[188] = {0};
    pkt[0] = 0x47;
    pkt[1] = 0x40 | 0x00; // PUSI, PID 0x11
    pkt[2] = 0x11;
    pkt[3] = 0x11; // Payload only
    pkt[4] = 0x00; // Pointer
    memcpy(pkt + 5, sdt, sizeof(sdt));

    ts_decode_result_t res;
    tsa_decode_packet(h, pkt, 1000, &res);

    assert(strcmp(h->service_name, "My Service 1") == 0);
    assert(strcmp(h->provider_name, "Provider") == 0);

    tsa_destroy(h);
    printf("test_sdt_assembly passed\n");
}

int main() {
    test_pat_assembly();
    test_sdt_assembly();
    return 0;
}
