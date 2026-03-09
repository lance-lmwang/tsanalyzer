#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "tsa.h"
#include "tsa_internal.h"

void test_h264_metadata_extraction() {
    printf("Running test_h264_metadata_extraction...\n");
    tsa_config_t cfg = {0};
    tsa_handle_t* h = tsa_create(&cfg);
    assert(h != NULL);

    // H.264 SPS for 1920x1080 (Main Profile)
    uint8_t sps[] = {0x00, 0x00, 0x00, 0x01, 0x67, 0x4d, 0x40, 0x28, 0xd9, 0x00, 0x78, 0x02, 0x27, 0xe5, 0x84,
                     0x00, 0x00, 0x03, 0x00, 0x04, 0x00, 0x00, 0x03, 0x00, 0xf2, 0x3c, 0x60, 0xc6, 0x58};

    // PES Header (Stream ID 0xE0, PTS only)
    uint8_t pes[] = {
        0x00, 0x00, 0x01, 0xe0, 0x00, 0x00, 0x80, 0x80, 0x05, 0x21, 0x00, 0x01, 0x00, 0x01  // PTS placeholder
    };

    uint8_t pkt[188] = {0};
    pkt[0] = TS_SYNC_BYTE;
    pkt[1] = 0x40 | 0x01;  // PID 0x100 + PUSI
    pkt[2] = 0x00;
    pkt[3] = TS_PAYLOAD_FLAG;

    memcpy(pkt + 4, pes, sizeof(pes));
    memcpy(pkt + 4 + sizeof(pes), sps, sizeof(sps));

    // Register PID 0x100 as H.264 via PMT
    uint8_t pmt[21] = {0x02, 0xb0, 0x12, 0x00, 0x01, 0xc1, 0x00, 0x00, 0xe1, 0x00, 0xf0,
                       0x00, 0x1b, 0xe1, 0x00, 0xf0, 0x00, 0x00, 0x00, 0x00, 0x00};
    uint32_t pmt_crc = mpegts_crc32(pmt, 17);
    pmt[17] = (pmt_crc >> 24) & 0xff;
    pmt[18] = (pmt_crc >> 16) & 0xff;
    pmt[19] = (pmt_crc >> 8) & 0xff;
    pmt[20] = pmt_crc & 0xff;

    uint8_t pmt_pkt[188] = {0};
    pmt_pkt[0] = TS_SYNC_BYTE;
    pmt_pkt[1] = 0x40 | 0x10;
    pmt_pkt[2] = 0x00;  // PID 0x1000 + PUSI
    pmt_pkt[3] = TS_PAYLOAD_FLAG;
    pmt_pkt[4] = 0x00;
    memcpy(pmt_pkt + 5, pmt, sizeof(pmt));

    // PAT to set PMT PID
    uint8_t pat[16] = {0x00, 0xb0, 0x0d, 0x00, 0x01, 0xc1,
                       0x00, 0x00, 0x00, 0x01, 0xf0, 0x00,  // Program 1 at PID 0x1000
                       0x00, 0x00, 0x00, 0x00};
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
    pat_pkt[2] = 0x00;
    pat_pkt[3] = TS_PAYLOAD_FLAG;
    pat_pkt[4] = 0x00;
    memcpy(pat_pkt + 5, pat, sizeof(pat));

    tsa_process_packet(h, pat_pkt, 1000000000ULL);
    tsa_process_packet(h, pmt_pkt, 1100000000ULL);
    tsa_process_packet(h, pkt, 1200000000ULL);

    // Send a second PUSI packet to flush the first one
    uint8_t pkt2[188];
    memcpy(pkt2, pkt, 188);
    tsa_process_packet(h, pkt2, 1300000000ULL);

    // Trigger snapshot
    tsa_commit_snapshot(h, 1500000000ULL);

    tsa_snapshot_full_t snap;
    tsa_take_snapshot_full(h, &snap);

    int idx = tsa_find_pid_in_snapshot(&snap, 0x100);
    assert(idx != -1);

    printf("Extracted Resolution: %ux%u, Profile: %u\n", snap.pids[idx].width, snap.pids[idx].height,
           snap.pids[idx].profile);

    assert(snap.pids[idx].width == 1920);
    assert(snap.pids[idx].height == 1080);

    tsa_destroy(h);
    printf("test_h264_metadata_extraction passed.\n");
}

int main() {
    test_h264_metadata_extraction();
    return 0;
}
