#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tsa.h"
#include "tsa_internal.h"
#include "tsa_packet_pool.h"

/* Helper to push raw ES data through the new PES accumulator */
static void push_es_as_pes(tsa_handle_t* h, uint16_t pid, const uint8_t* es_data, size_t es_len) {
    /* 1. Create a fake PES packet (Header + ES) */
    uint8_t pes_header[] = {0x00, 0x00, 0x01, 0xE0, 0x00, 0x00, 0x80, 0x00, 0x00};
    size_t total_len = sizeof(pes_header) + es_len;

    /* 2. Allocate and wrap in tsa_packet_t */
    tsa_packet_t* p = tsa_packet_pool_acquire(h->pkt_pool);
    /* In TS, payload starts at offset 4 (assuming no adaptation field) */
    memcpy(p->data + 4, pes_header, sizeof(pes_header));
    memcpy(p->data + 4 + sizeof(pes_header), es_data, es_len);

    /* 3. Simulate a PUSI packet for the accumulator */
    ts_decode_result_t res = {0};
    res.pid = pid;
    res.pusi = true;
    res.has_payload = true;
    res.payload_len = (int)total_len;
    res.af_len = 0;
    res.has_pes_header = true;

    tsa_es_track_push_packet(h, pid, p->data, &res);

    /* 4. Flush the accumulator by pushing another PUSI (or calling finalize) */
    ts_decode_result_t flush_res = {0};
    flush_res.pid = pid;
    flush_res.pusi = true;
    flush_res.has_payload = false;
    tsa_es_track_push_packet(h, pid, NULL, &flush_res);
}

void test_h264_frame_counting() {
    printf("Running test_h264_frame_counting...\n");
    tsa_config_t cfg = {0};
    tsa_handle_t* h = tsa_create(&cfg);
    uint16_t pid = 0x100;
    h->live->pid_is_referenced[pid] = true;
    h->pid_seen[pid] = true;

    // Register in active list
    uint8_t dummy_pkt[188] = {0x47, 0x01, 0x00, 0x10};
    tsa_process_packet(h, dummy_pkt, 500000);

    h->program_count = 1;
    h->programs[0].stream_count = 1;
    h->programs[0].streams[0].pid = pid;
    h->programs[0].streams[0].stream_type = 0x1b;
    h->es_tracks[pid].stream_type = 0x1b;

    // 1. SPS
    uint8_t sps[] = {0x00, 0x00, 0x01, 0x67, 0x42, 0x00, 0x0a, 0xf8, 0x41, 0xa2};
    push_es_as_pes(h, pid, sps, sizeof(sps));

    // 2. IDR
    uint8_t idr[] = {0x00, 0x00, 0x01, 0x65, 0xb0, 0x00, 0x00, 0x00, 0x00};
    push_es_as_pes(h, pid, idr, sizeof(idr));

    // 3. P-Slice
    uint8_t p_slice[] = {0x00, 0x00, 0x01, 0x61, 0xe2, 0x00, 0x00, 0x00, 0x00};
    push_es_as_pes(h, pid, p_slice, sizeof(p_slice));

    // 4. B-Slice
    uint8_t b_slice[] = {0x00, 0x00, 0x01, 0x61, 0xa9, 0x00, 0x00, 0x00, 0x00};
    push_es_as_pes(h, pid, b_slice, sizeof(b_slice));

    tsa_commit_snapshot(h, 5000000);
    tsa_snapshot_full_t snap;
    tsa_take_snapshot_full(h, &snap);

    int idx = tsa_find_pid_in_snapshot(&snap, pid);
    assert(idx != -1);

    printf("Frames - I: %llu, P: %llu, B: %llu\n", (unsigned long long)snap.pids[idx].i_frames,
           (unsigned long long)snap.pids[idx].p_frames, (unsigned long long)snap.pids[idx].b_frames);

    assert(snap.pids[idx].i_frames == 1);
    assert(snap.pids[idx].p_frames == 1);
    assert(snap.pids[idx].b_frames == 1);

    tsa_destroy(h);
    printf("test_h264_frame_counting passed.\n");
}

int main() {
    test_h264_frame_counting();
    return 0;
}
