#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tsa.h"
#include "tsa_internal.h"

int main() {
    printf(">>> STARTING REAL-DATA ES PARSING UNIT TEST <<<\n");

    tsa_config_t cfg = {0};
    tsa_handle_t* h = tsa_create(&cfg);
    assert(h != NULL);

    uint16_t pid = 0x100;
    tsa_es_track_t* es = &h->es_tracks[pid];
    es->stream_type = TSA_TYPE_VIDEO_H264;
    h->pid_seen[pid] = true;
    h->signal_lock = true;
    h->sync_state = TS_SYNC_LOCKED;

    /* IDR Packet with PES Header */
    uint8_t idr_pkt[188];
    memset(idr_pkt, 0xFF, 188);
    idr_pkt[0] = 0x47;
    idr_pkt[1] = 0x41;
    idr_pkt[2] = 0x00;
    // PES Header (9 bytes) + NALU Start Code (3 bytes) + IDR Header (1 byte)
    idr_pkt[4] = 0x00;
    idr_pkt[5] = 0x00;
    idr_pkt[6] = 0x01;
    idr_pkt[7] = 0xE0;
    idr_pkt[8] = 0x00;
    idr_pkt[9] = 0x20;
    idr_pkt[10] = 0x80;
    idr_pkt[11] = 0x00;
    idr_pkt[12] = 0x00;
    idr_pkt[13] = 0x00;
    idr_pkt[14] = 0x00;
    idr_pkt[15] = 0x01;
    idr_pkt[16] = 0x65;  // IDR
    idr_pkt[17] = 0x00;  // Extra byte to fill up to NALU start if needed

    /* Non-IDR (P-frame) Packet */
    uint8_t p_pkt[188];
    memset(p_pkt, 0xFF, 188);
    p_pkt[0] = 0x47;
    p_pkt[1] = 0x41;
    p_pkt[2] = 0x00;
    p_pkt[4] = 0x00;
    p_pkt[5] = 0x00;
    p_pkt[6] = 0x01;
    p_pkt[7] = 0xE0;
    p_pkt[8] = 0x00;
    p_pkt[9] = 0x00;
    p_pkt[10] = 0x00;
    p_pkt[11] = 0x00;
    p_pkt[12] = 0x00;
    p_pkt[13] = 0x00;
    p_pkt[14] = 0x00;
    p_pkt[15] = 0x00;
    p_pkt[16] = 0x01;
    p_pkt[17] = 0x41;  // P-Frame

    printf("Feeding IDR stream...\n");
    for (int i = 0; i < 5; i++) {
        idr_pkt[3] = 0x10 + (i % 16);  // Correct CC increment
        tsa_feed_data(h, idr_pkt, 188, 1000000000ULL + i * 1000);
    }

    printf("Feeding P-Frame PUSI (triggers IDR analysis)...\n");
    p_pkt[3] = 0x15;  // Continue CC
    tsa_feed_data(h, p_pkt, 188, 1100000000ULL);

    /* Finalize the P-frame analysis by feeding another PUSI */
    p_pkt[3] = 0x16;
    tsa_feed_data(h, p_pkt, 188, 1200000000ULL);

    printf("GOP Structure Result: [%s]\n", es->video.gop_structure);
    printf("I-Frame Size: %lu\n", (unsigned long)es->video.i_frame_size_bytes);

    // CRITICAL: We use hard exit on failure now
    if (strchr(es->video.gop_structure, 'I') == NULL) {
        printf("FAILED: 'I' not found in GOP string\n");
        exit(1);
    }

    if (es->video.i_frame_size_bytes == 0) {
        printf("FAILED: I-frame size is 0\n");
        exit(1);
    }

    tsa_destroy(h);
    printf(">>> REAL-DATA ES PARSING UNIT TEST PASSED <<<\n");
    return 0;
}
