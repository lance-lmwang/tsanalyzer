#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "tsa.h"
#include "tsa_internal.h"

// Mock PAT: TID=0, TSID=0x0099, Version=10, Program 1 -> PMT 0x1BE9
// Data: 00 b0 0d 00 99 e1 00 00 00 01 bb e9
// CRC calculated: 0xBC447FE0
static uint8_t mock_pat_packet[188] = {0x47, 0x40, 0x00, 0x10,        // Header: PUSI=1, PID=0, CC=0
                                       0x00,                          // Pointer Field = 0
                                       0x00, 0xb0, 0x0d,              // Section Header: TID=0, Len=13
                                       0x00, 0x99, 0xe1, 0x00, 0x00,  // TSID, Version, SecNum, LastSecNum
                                       0x00, 0x01, 0xbb, 0xe9,        // Program 1 -> PMT PID 0x1BE9
                                       0xbc, 0x44, 0x7f, 0xe0         // CRC32
};

// Mock PMT: TID=2, Prog=1, Version=0, PCR=0x0202, Stream=H.264 (0x1B) on 0x0202
// Data: 02 b0 12 00 01 c1 00 00 e2 02 f0 00 1b e2 02 f0 00
// CRC calculated: 0xD3F8FCBF
static uint8_t mock_pmt_packet_with_af[188] = {0x47, 0x5b, 0xe9, 0x30,  // Header: PUSI=1, PID=0x1BE9, AF+Payload, CC=0
                                               0x09, 0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,  // AF: Len 9
                                               0x00,                          // Pointer Field = 0
                                               0x02, 0xb0, 0x12,              // Section Header: TID=2 (PMT), Len=18
                                               0x00, 0x01, 0xc1, 0x00, 0x00,  // Program 1, Version, etc.
                                               0xe2, 0x02,                    // PCR PID 0x0202
                                               0xf0, 0x00,                    // Program Info Len 0
                                               0x1b, 0xe2, 0x02, 0xf0, 0x00,  // Stream: H.264, PID 0x0202
                                               0xd3, 0xf8, 0xfc, 0xbf         // CRC32
};

void test_crc32_vectors() {
    printf("Testing CRC32 standard vectors...\n");
    uint8_t* data = &mock_pat_packet[5];
    uint32_t res = mpegts_crc32(data, 16);
    if (res != 0) {
        printf("  CRC mismatch! Calculated: 0x%08x, Expected: 0\n", res);
    }
    assert(res == 0);
    printf("  CRC32 verification passed.\n");
}

void test_psi_full_chain() {
    printf("Testing PSI analysis chain (PAT -> PMT -> Service Tree)....\n");
    tsa_config_t cfg = {0};
    tsa_handle_t* h = tsa_create(&cfg);

    // Process PAT: This should register 0x1BE9 as a PMT PID
    tsa_process_packet(h, mock_pat_packet, 1000000);

    // Process PMT: Validates section parsing and ES discovery
    tsa_process_packet(h, mock_pmt_packet_with_af, 2000000);
    tsa_commit_snapshot(h, 3000000);

    tsa_snapshot_full_t stats;
    tsa_take_snapshot_full(h, &stats);

    assert(stats.stats.crc_error.count == 0);
    assert(stats.stats.pid_is_referenced[0x1BE9] == true);
    assert(stats.stats.pid_is_referenced[0x0202] == true);

    tsa_destroy(h);
    printf("  PSI analysis chain passed.\n");
}

void test_pointer_field_offset() {
    printf("Testing Pointer Field offset handling...\n");
    uint8_t pkt[188];
    memset(pkt, 0xff, 188);
    pkt[0] = 0x47;
    pkt[1] = 0x40;
    pkt[2] = 0x00;
    pkt[3] = 0x10;                              // PAT header
    pkt[4] = 0x05;                              // Pointer Field = 5 (skip 5 bytes of padding)
    memset(&pkt[5], 0xAA, 5);                   // Dummy data
    memcpy(&pkt[10], &mock_pat_packet[5], 16);  // Actual Section

    tsa_config_t cfg = {0};
    tsa_handle_t* h = tsa_create(&cfg);
    tsa_process_packet(h, pkt, 1000000);
    tsa_commit_snapshot(h, 2000000);

    tsa_snapshot_full_t stats;
    tsa_take_snapshot_full(h, &stats);
    assert(stats.stats.crc_error.count == 0);

    tsa_destroy(h);
    printf("  Pointer Field offset handling passed.\n");
}

int main() {
    test_crc32_vectors();
    test_psi_full_chain();
    test_pointer_field_offset();
    printf("ALL PSI DEEP TESTS PASSED!\n");
    return 0;
}
