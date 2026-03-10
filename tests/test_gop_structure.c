#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "tsa.h"
#include "tsa_internal.h"

int main() {
    printf(">>> STARTING GOP STRUCTURE TRACKING TEST <<<\n");

    tsa_config_t cfg = {0};
    tsa_handle_t* h = tsa_create(&cfg);
    assert(h != NULL);

    uint16_t pid = 0x100;
    tsa_es_track_t* es = &h->es_tracks[pid];
    es->stream_type = TSA_TYPE_VIDEO_H264;
    h->pid_seen[pid] = true;
    h->signal_lock = true;

    /* Simulate I-Frame (IDR) */
    ts_decode_result_t res = {0};
    res.pid = pid;
    res.pusi = true;
    res.has_payload = true;
    res.payload_len = 100;
    res.has_pes_header = true;

    // We mock the analyzer result: IDR + Slice
    // Instead of raw NALU parsing, we set the discovered type from tsa_es.c
    es->pes.current_frame_type = 'I';
    es->pes.ref_count = 1;
    es->pes.total_length = 50000;  // 50KB I-Frame

    /* Feed a PUSI to complete the previous frame and record stats */
    uint8_t dummy[188];
    memset(dummy, 0, 188);
    dummy[0] = 0x47;
    dummy[1] = 0x41;
    dummy[2] = 0x00;
    dummy[3] = 0x10;

    tsa_feed_data(h, dummy, 188, 1000000000ULL);

    // Setup next frame as 'P'
    es->pes.current_frame_type = 'P';
    es->pes.ref_count = 1;
    es->pes.total_length = 10000;  // 10KB P-Frame
    tsa_feed_data(h, dummy, 188, 1100000000ULL);

    printf("GOP String: %s\n", es->video.gop_structure);
    printf("I-Frame Stats: %lu bytes\n", (unsigned long)es->video.i_frame_size_bytes);

    assert(strstr(es->video.gop_structure, "IP") != NULL);
    assert(es->video.i_frame_size_bytes == 50000);
    assert(es->video.p_frame_size_bytes == 10000);

    tsa_destroy(h);
    printf(">>> GOP STRUCTURE TRACKING TEST PASSED <<<\n");
    return 0;
}
