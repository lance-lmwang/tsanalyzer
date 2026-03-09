#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tsa.h"
#include "tsa_internal.h"

// Simulate PMT reference
void mock_pmt_reference(tsa_handle_t* h, uint16_t pid, uint8_t stream_type) {
    h->live->pid_is_referenced[pid] = true;
    h->pid_seen[pid] = true;
    h->pid_stream_type[pid] = stream_type;
    h->program_count = 1;
    h->programs[0].stream_count = 1;
    h->programs[0].streams[0].pid = pid;
    h->programs[0].streams[0].stream_type = stream_type;
}

static void feed_es_payload(tsa_handle_t* h, uint16_t pid, const uint8_t* data, int len) {
    uint8_t pkt[188];
    int processed = 0;
    while (processed < len) {
        memset(pkt, 0, 188);
        pkt[0] = 0x47;
        pkt[1] = (pid >> 8) & 0x1F;
        pkt[2] = pid & 0xFF;
        pkt[3] = 0x10;                       // Payload only
        if (processed == 0) pkt[1] |= 0x40;  // PUSI

        int to_copy = (len - processed > 184) ? 184 : (len - processed);
        memcpy(pkt + 4, data + processed, to_copy);

        tsa_process_packet(h, pkt, 1000000 + processed * 100);
        processed += to_copy;
    }

    // Trigger final flush by sending PUSI of next packet
    memset(pkt, 0, 188);
    pkt[0] = 0x47;
    pkt[1] = 0x40 | ((pid >> 8) & 0x1F);
    pkt[2] = pid & 0xFF;
    pkt[3] = 0x10;
    tsa_process_packet(h, pkt, 2000000);
}

void test_h264_sps() {
    printf("Testing H.264 SPS parsing (Zero-Copy Path)...\n");
    tsa_handle_t* h = tsa_create(NULL);
    uint16_t pid = 200;
    mock_pmt_reference(h, pid, 0x1B);

    uint8_t h264_sps[] = {0x00, 0x00, 0x01, 0x67, 0x64, 0x00, 0x28, 0xAC, 0x2B,
                          0x40, 0x3C, 0x01, 0x13, 0xF2, 0xC0, 0x3C, 0x4D, 0x40};

    feed_es_payload(h, pid, h264_sps, sizeof(h264_sps));

    assert(h->es_tracks[pid].video.width == 1920);
    assert(h->es_tracks[pid].video.height == 1080);

    tsa_destroy(h);
    printf("H.264 OK.\n");
}

void test_h265_sps() {
    printf("Testing H.265 SPS parsing (Zero-Copy Path)...\n");
    tsa_handle_t* h = tsa_create(NULL);
    uint16_t pid = 201;
    mock_pmt_reference(h, pid, 0x24);

    uint8_t h265_sps[] = {0x00, 0x00, 0x01, 0x42, 0x01, 0x01, 0x01, 0x60, 0x00, 0x00, 0x03,
                          0x00, 0x80, 0x00, 0x00, 0x03, 0x00, 0x00, 0x3C, 0xA0, 0x08, 0x08,
                          0x0A, 0x05, 0x89, 0x08, 0xDA, 0x05, 0xC1, 0x2C, 0x1C, 0x10};

    feed_es_payload(h, pid, h265_sps, sizeof(h265_sps));

    assert(h->es_tracks[pid].video.width > 0);

    tsa_destroy(h);
    printf("H.265 OK.\n");
}

int main() {
    test_h264_sps();
    test_h265_sps();
    printf("All ES Deep Analysis tests PASSED!\n");
    return 0;
}
