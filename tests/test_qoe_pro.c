#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tsa.h"
#include "tsa_internal.h"

void test_h264_frame_counting() {
    printf("Running test_h264_frame_counting...\n");
    tsa_config_t cfg = {0};
    tsa_handle_t* h = tsa_create(&cfg);
    uint16_t pid = 0x100;
    h->live.pid_is_referenced[pid] = true;
    h->pid_seen[pid] = true;  // CRITICAL FIX

    // Register in active list
    uint8_t dummy_pkt[188] = {0x47, 0x01, 0x00, 0x10};
    tsa_process_packet(h, dummy_pkt, 500000);

    h->program_count = 1;
    h->programs[0].stream_count = 1;
    h->programs[0].streams[0].pid = pid;
    h->programs[0].streams[0].stream_type = 0x1b;

    // 1. SPS
    uint8_t sps[] = {0x00, 0x00, 0x01, 0x67, 0x42, 0x00, 0x0a, 0xf8, 0x41, 0xa2};
    tsa_handle_es_payload(h, pid, sps, sizeof(sps), 1000000);

    // 2. IDR
    uint8_t idr[] = {0x00, 0x00, 0x01, 0x65, 0xb0, 0x00, 0x00, 0x00, 0x00};
    tsa_handle_es_payload(h, pid, idr, sizeof(idr), 2000000);

    // 3. P-Slice
    uint8_t p_slice[] = {0x00, 0x00, 0x01, 0x61, 0xe2, 0x00, 0x00, 0x00, 0x00};
    tsa_handle_es_payload(h, pid, p_slice, sizeof(p_slice), 3000000);

    // 4. B-Slice
    uint8_t b_slice[] = {0x00, 0x00, 0x01, 0x61, 0xa9, 0x00, 0x00, 0x00, 0x00};
    tsa_handle_es_payload(h, pid, b_slice, sizeof(b_slice), 4000000);

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
