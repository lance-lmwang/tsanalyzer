#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

#include "tsa.h"
#include "tsa_internal.h"

static const uint8_t ltn_uuid_sei_timestamp[] = {0x59, 0x96, 0xFF, 0x28, 0x17, 0xCA, 0x41, 0x96,
                                                 0x8D, 0xE3, 0xE5, 0x3F, 0xE2, 0xF9, 0x92, 0xAE};

/**
 * @brief Unit test for SEI-based E2E latency forensic analysis.
 * Simulates an encoder SEI message and verifies latency calculation.
 */
int main() {
    printf(">>> STARTING SEI LATENCY FORENSIC TEST <<<\n");

    tsa_config_t cfg = {0};
    tsa_handle_t* h = tsa_create(&cfg);
    assert(h != NULL);

    uint16_t pid = 0x100;
    tsa_es_track_t* es = &h->es_tracks[pid];
    es->stream_type = TSA_TYPE_VIDEO_H264;
    h->pid_seen[pid] = true;
    h->signal_lock = true;
    h->sync_state = TS_SYNC_LOCKED;

    /* 1. Construct SEI Payload (64 bytes) */
    uint8_t sei_payload[128];
    memset(sei_payload, 0, sizeof(sei_payload));
    memcpy(sei_payload, ltn_uuid_sei_timestamp, 16);

    uint32_t v[8] = {0};
    v[4] = 100;
    v[5] = 0;
    v[6] = 100;
    v[7] = 100000;  // 100ms latency

    uint8_t* p = sei_payload + 16;
    for (int i = 0; i < 8; i++) {
        p[0] = (v[i] >> 24) & 0xFF;
        p[1] = (v[i] >> 16) & 0xFF;
        p[2] = 0xAA;
        p[3] = (v[i] >> 8) & 0xFF;
        p[4] = v[i] & 0xFF;
        p[5] = 0xAA;
        p += 6;
    }

    /* 2. Construct TS Packet with SEI */
    uint8_t pkt[188];
    memset(pkt, 0xFF, 188);
    pkt[0] = 0x47;
    pkt[1] = 0x41;
    pkt[2] = 0x00;
    pkt[3] = 0x10;
    pkt[4] = 0x00;  // Pointer
    pkt[5] = 0x00;
    pkt[6] = 0x00;
    pkt[7] = 0x01;
    pkt[8] = 0xE0;
    pkt[9] = 0x00;
    pkt[10] = 0x50;  // Length
    pkt[11] = 0x80;
    pkt[12] = 0x00;
    pkt[13] = 0x00;

    // SEI NALU: 00 00 01 06 05 40 <payload>
    pkt[14] = 0x00;
    pkt[15] = 0x00;
    pkt[16] = 0x01;
    pkt[17] = 0x06;  // SEI
    pkt[18] = 0x05;  // User data unregistered
    pkt[19] = 64;    // Size
    memcpy(pkt + 20, sei_payload, 64);

    /* 3. Feed Data */
    printf("Feeding SEI packet...\n");
    tsa_feed_data(h, pkt, 188, 2000000000ULL);

    // Trigger analysis
    uint8_t trigger[188];
    memset(trigger, 0xFF, 188);
    trigger[0] = 0x47;
    trigger[1] = 0x41;
    trigger[2] = 0x00;
    trigger[3] = 0x11;
    tsa_feed_data(h, trigger, 188, 2100000000ULL);

    /* 4. Verify Metrics */
    printf("Encoder Latency: %lu ms\n", (unsigned long)es->video.encoder_latency_ms);

    if (es->video.encoder_latency_ms != 100) {
        printf("FAILED: Expected 100ms encoder latency, got %lu\n", (unsigned long)es->video.encoder_latency_ms);
        exit(1);
    }

    tsa_destroy(h);
    printf(">>> SEI LATENCY FORENSIC TEST PASSED <<<\n");
    return 0;
}
