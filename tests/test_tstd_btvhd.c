#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "tsa.h"
#include "tsa_internal.h"

// Auto-generate test data directly instead of relying on sample/test.ts
static uint32_t crc32_table[256];
static void init_crc32() {
    for (int i = 0; i < 256; i++) {
        uint32_t crc = i << 24;
        for (int j = 0; j < 8; j++) crc = (crc << 1) ^ (crc & 0x80000000 ? 0x04C11DB7 : 0);
        crc32_table[i] = crc;
    }
}
static uint32_t get_crc32(const uint8_t* data, int len) {
    uint32_t crc = 0xFFFFFFFF;
    for (int i = 0; i < len; i++) crc = (crc << 8) ^ crc32_table[((crc >> 24) ^ data[i]) & 0xFF];
    return crc;
}

static uint8_t h264_sps_pps[] = {
    0x00, 0x00, 0x00, 0x01, 0x67, 0x42, 0x00, 0x1f, 0x95, 0xa8, 0x1e, 0x00, 0x5b, 0x90, // SPS 720p
    0x00, 0x00, 0x00, 0x01, 0x68, 0xce, 0x3c, 0x80                                     // PPS
};

static void write_pat(uint8_t* p, uint8_t cc) {
    memset(p, 0xFF, 188);
    p[0] = 0x47; p[1] = 0x40; p[2] = 0x00; p[3] = 0x10 | (cc & 0x0F); p[4] = 0x00;
    uint8_t section[] = {
        0x00, 0xB0, 0x0D, 0x00, 0x01, 0xC1, 0x00, 0x00, 0x00, 0x01, 0xE0 | (0x1000 >> 8), 0x1000 & 0xFF
    };
    memcpy(p + 5, section, sizeof(section));
    uint32_t crc = get_crc32(p + 5, sizeof(section));
    p[5 + sizeof(section) + 0] = (crc >> 24) & 0xFF;
    p[5 + sizeof(section) + 1] = (crc >> 16) & 0xFF;
    p[5 + sizeof(section) + 2] = (crc >> 8) & 0xFF;
    p[5 + sizeof(section) + 3] = crc & 0xFF;
}

static void write_pmt(uint8_t* p, uint8_t cc) {
    memset(p, 0xFF, 188);
    p[0] = 0x47; p[1] = 0x40 | (0x1000 >> 8); p[2] = 0x1000 & 0xFF; p[3] = 0x10 | (cc & 0x0F); p[4] = 0x00;
    uint8_t section[] = {
        0x02, 0xB0, 0x17, 0x00, 0x01, 0xC1, 0x00, 0x00,
        0xE0 | (0x0100 >> 8), 0x0100 & 0xFF,
        0xF0, 0x00,
        0x1B, 0xE0 | (0x0100 >> 8), 0x0100 & 0xFF, 0xF0, 0x00 // H.264 video at 0x0100
    };
    memcpy(p + 5, section, sizeof(section));
    uint32_t crc = get_crc32(p + 5, sizeof(section));
    p[5 + sizeof(section) + 0] = (crc >> 24) & 0xFF;
    p[5 + sizeof(section) + 1] = (crc >> 16) & 0xFF;
    p[5 + sizeof(section) + 2] = (crc >> 8) & 0xFF;
    p[5 + sizeof(section) + 3] = crc & 0xFF;
}

static void write_video(uint8_t* p, uint8_t cc) {
    memset(p, 0xFF, 188);
    p[0] = 0x47; p[1] = 0x40 | (0x0100 >> 8); p[2] = 0x0100 & 0xFF; p[3] = 0x10 | (cc & 0x0F);
    p[4] = 0x00; p[5] = 0x00; p[6] = 0x01; p[7] = 0xe0; // PES
    p[8] = 0x00; p[9] = 0x00; p[10] = 0x80; p[11] = 0x80; p[12] = 0x05;
    p[13] = 0x21; p[14] = 0x00; p[15] = 0x01; p[16] = 0x00; p[17] = 0x01;
    memcpy(p + 18, h264_sps_pps, sizeof(h264_sps_pps));
}

int main() {
    printf("Running In-Memory Video Metadata Test (No external file dependency)...\n");
    init_crc32();

    tsa_config_t cfg = {0};
    cfg.is_live = false;
    tsa_handle_t* h = tsa_create(&cfg);
    assert(h);

    uint8_t pkt[188];
    uint64_t now = 1000000000ULL;

    write_pat(pkt, 0);
    tsa_process_packet(h, pkt, now); now += 100000;

    write_pmt(pkt, 0);
    tsa_process_packet(h, pkt, now); now += 100000;

    write_video(pkt, 0);
    tsa_process_packet(h, pkt, now); now += 100000;

    // Send a second PUSI packet to flush the first one
    uint8_t pkt2[188];
    memcpy(pkt2, pkt, 188);
    tsa_process_packet(h, pkt2, now); now += 100000;

    tsa_commit_snapshot(h, now + 10000000ULL);

    tsa_snapshot_full_t snap;
    tsa_take_snapshot_full(h, &snap);

    int idx = tsa_find_pid_in_snapshot(&snap, 0x0100);
    assert(idx != -1);

    printf("Found Video PID 0x0100: %dx%d\n", snap.pids[idx].width, snap.pids[idx].height);
    assert(snap.pids[idx].width == 1280);
    assert(snap.pids[idx].height == 720);

    tsa_destroy(h);

    printf("In-Memory Video Metadata Test PASSED!\n");
    return 0;
}