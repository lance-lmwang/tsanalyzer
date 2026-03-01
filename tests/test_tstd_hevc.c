#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tsa.h"
#include "tsa_internal.h"

void test_hevc_tstd_logic() {
    printf("Running test_hevc_tstd_logic...\n");
    tsa_config_t cfg = {0};
    tsa_handle_t* h = tsa_create(&cfg);

    uint16_t video_pid = 0x101;
    h->pid_stream_type[video_pid] = 0x24;  // Manually mark as HEVC
    h->live->pid_is_referenced[video_pid] = true;

    uint8_t pkt[188] = {0x47, 0x01, 0x01, 0x10};  // PID 0x101, Payload only
    uint64_t now_ns = 1000000000ULL;

    for (int i = 0; i < 1000; i++) {
        now_ns += 30000;
        tsa_process_packet(h, pkt, now_ns);
    }

    tsa_commit_snapshot(h, now_ns);
    tsa_snapshot_full_t snap;
    tsa_take_snapshot_full(h, &snap);

    printf("HEVC EB Fill: %u bytes\n", snap.stats.pid_eb_fill_bytes[video_pid]);
    assert(snap.stats.pid_eb_fill_bytes[video_pid] > 0);

    uint16_t audio_pid = 0x102;
    h->pid_stream_type[audio_pid] = 0x0f;  // ADTS-AAC
    h->live->pid_is_referenced[audio_pid] = true;
    pkt[2] = 0x02;

    for (int i = 0; i < 1000; i++) {
        now_ns += 300000;
        tsa_process_packet(h, pkt, now_ns);
    }
    tsa_commit_snapshot(h, now_ns);
    tsa_take_snapshot_full(h, &snap);
    printf("Audio EB Fill: %u bytes\n", snap.stats.pid_eb_fill_bytes[audio_pid]);

    tsa_destroy(h);
}

int main() {
    test_hevc_tstd_logic();
    return 0;
}
