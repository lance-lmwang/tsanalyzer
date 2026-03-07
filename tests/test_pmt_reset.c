#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "tsa.h"
#include "tsa_internal.h"

void test_pmt_context_reset() {
    printf("Running test_pmt_context_reset...\n");
    tsa_config_t cfg = {0};
    tsa_handle_t* h = tsa_create(&cfg);
    assert(h != NULL);

    uint8_t pkt_data[188] = {0};
    pkt_data[0] = TS_SYNC_BYTE;
    pkt_data[1] = 0x01;  // PID 0x100
    pkt_data[2] = 0x00;
    pkt_data[3] = TS_PAYLOAD_FLAG;

    // 1. Fill buffers for PID 0x100
    tsa_process_packet(h, pkt_data, 1000000000ULL);
    uint32_t initial_fill = tsa_get_pid_tb_fill(h, 0x100);
    printf("Initial TB Fill: %u\n", initial_fill);
    assert(initial_fill > 0);

    // Register PID 0x1000 as PMT via a PAT first
    uint8_t pat[16] = {
        0x00, 0xb0, 0x0d, 0x00, 0x01, 0xc1, 0x00, 0x00, 0x00, 0x01, 0xf0, 0x00,  // Program 1 at PID 0x1000
        0x00, 0x00, 0x00, 0x00                                                   // CRC placeholder
    };
    pat[10] = 0xf0 | (0x1000 >> 8);
    pat[11] = 0x1000 & 0xff;
    uint32_t pat_crc = mpegts_crc32(pat, 12);
    pat[12] = (pat_crc >> 24) & 0xff;
    pat[13] = (pat_crc >> 16) & 0xff;
    pat[14] = (pat_crc >> 8) & 0xff;
    pat[15] = pat_crc & 0xff;

    uint8_t pat_pkt[188] = {0};
    pat_pkt[0] = TS_SYNC_BYTE;
    pat_pkt[1] = 0x40;
    pat_pkt[2] = 0x00;  // PAT PID + PUSI
    pat_pkt[3] = TS_PAYLOAD_FLAG;
    pat_pkt[4] = 0x00;  // Pointer field
    memcpy(pat_pkt + 5, pat, sizeof(pat));

    tsa_process_packet(h, pat_pkt, 1100000000ULL);

    // 2. Simulate PMT change (re-defining PID 0x100)
    uint8_t pmt[21] = {
        0x02, 0xb0, 0x12, 0x00, 0x01, 0xc1, 0x00, 0x00, 0xe1, 0x00, 0xf0, 0x00,  // PCR_PID=0x100
        0x1b, 0xe1, 0x00, 0xf0, 0x00,                                            // Stream 0x1b (H.264) at PID 0x100
        0x00, 0x00, 0x00, 0x00                                                   // CRC placeholder
    };
    uint32_t pmt_crc = mpegts_crc32(pmt, 17);
    pmt[17] = (pmt_crc >> 24) & 0xff;
    pmt[18] = (pmt_crc >> 16) & 0xff;
    pmt[19] = (pmt_crc >> 8) & 0xff;
    pmt[20] = pmt_crc & 0xff;

    uint8_t pmt_pkt[188] = {0};
    pmt_pkt[0] = TS_SYNC_BYTE;
    pmt_pkt[1] = 0x50;
    pmt_pkt[2] = 0x00;  // PID 0x1000 + PUSI
    pmt_pkt[3] = TS_PAYLOAD_FLAG;
    pmt_pkt[4] = 0x00;  // Pointer field
    memcpy(pmt_pkt + 5, pmt, sizeof(pmt));

    tsa_process_packet(h, pmt_pkt, 1200000000ULL);

    // After PMT update, PID 0x100 buffer should be reset
    uint32_t fill = tsa_get_pid_tb_fill(h, 0x100);
    printf("TB Fill after PMT update: %u\n", fill);

    // RED PHASE: This should fail because we haven't implemented reset logic yet.
    assert(fill == 0);

    tsa_destroy(h);
    printf("test_pmt_context_reset passed.\n");
}

int main() {
    test_pmt_context_reset();
    return 0;
}
