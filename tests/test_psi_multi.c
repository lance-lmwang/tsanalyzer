#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tsa.h"
#include "tsa_internal.h"

// Large PMT Section: TID=2, Len=200 (Total 203 bytes), spread across 2 packets
void test_multi_packet_psi() {
    printf("Testing Multi-packet PSI reassembly...\n");
    tsa_config_t cfg = {0};
    tsa_handle_t* h = tsa_create(&cfg);

    // 1. Inject PAT to register PMT PID 0x1BE9
    uint8_t mock_pat[188] = {0x47, 0x40, 0x00, 0x10, 0x00, 0x00, 0xb0, 0x0d, 0x00, 0x99, 0xe1,
                             0x00, 0x00, 0x00, 0x01, 0xbb, 0xe9, 0xbc, 0x44, 0x7f, 0xe0};
    tsa_process_packet(h, mock_pat, 2000);

    // 2. Construct a 200-byte PMT for PID 0x1BE9
    uint8_t pmt_large[203];
    memset(pmt_large, 0, 203);
    pmt_large[0] = 0x02;  // PMT
    pmt_large[1] = 0xb0;
    pmt_large[2] = 200;  // Len 200
    pmt_large[3] = 0x00;
    pmt_large[4] = 0x01;  // Prog 1
    pmt_large[5] = 0xc1;  // Version
    // Fill with some dummy ES data to reach target length
    for (int i = 12; i < 199; i++) pmt_large[i] = 0xAA;
    // Calculate CRC for this 203 byte block
    uint32_t crc = mpegts_crc32(pmt_large, 199);
    pmt_large[199] = (crc >> 24) & 0xff;
    pmt_large[200] = (crc >> 16) & 0xff;
    pmt_large[201] = (crc >> 8) & 0xff;
    pmt_large[202] = crc & 0xff;

    // 3. Inject Packet 1 (PUSI=1, contains first 183 bytes of section)
    uint8_t pkt1[188];
    memset(pkt1, 0xff, 188);
    pkt1[0] = 0x47;
    pkt1[1] = 0x5b;
    pkt1[2] = 0xe9;
    pkt1[3] = 0x10;  // PUSI=1, PID 0x1BE9, CC=0
    pkt1[4] = 0x00;  // Pointer
    memcpy(&pkt1[5], pmt_large, 183);
    tsa_process_packet(h, pkt1, 3000);

    // 4. Inject Packet 2 (PUSI=0, contains remaining 20 bytes)
    uint8_t pkt2[188];
    memset(pkt2, 0xff, 188);
    pkt2[0] = 0x47;
    pkt2[1] = 0x1b;
    pkt2[2] = 0xe9;
    pkt2[3] = 0x11;  // PUSI=0, PID 0x1BE9, CC=1
    memcpy(&pkt2[4], &pmt_large[183], 20);
    tsa_process_packet(h, pkt2, 4000);

    // 5. Verify
    tsa_commit_snapshot(h, 5000);
    tsa_snapshot_full_t stats;
    tsa_take_snapshot_full(h, &stats);

    assert(stats.stats.crc_error_count == 0);
    // Referenced PIDs should be captured if PMT was parsed
    assert(stats.stats.pid_is_referenced[0x1BE9] == true);

    tsa_destroy(h);
    printf("  Multi-packet PSI reassembly passed!\n");
}

int main() {
    test_multi_packet_psi();
    return 0;
}
