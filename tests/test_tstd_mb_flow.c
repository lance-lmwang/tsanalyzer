#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "tsa.h"
#include "tsa_internal.h"

/* Ensure asserts are working */
#ifdef NDEBUG
#error "This test MUST be compiled without NDEBUG to enable assertions!"
#endif

void test_tstd_mb_flow_backpressure() {
    printf("Running test_tstd_mb_flow_backpressure (EB Full scenario)...\n");
    tsa_config_t cfg = {0};
    tsa_handle_t* h = tsa_create(&cfg);

    uint16_t pid = 0x100;
    h->pid_stream_type[pid] = TSA_TYPE_VIDEO_H264;
    h->live->pid_is_referenced[pid] = true;
    h->live->pid_bitrate_bps[pid] = 10000000;  // 10 Mbps

    tsa_es_track_t* es = &h->es_tracks[pid];
    uint64_t now = 1000000000ULL;
    h->stc_ns = now;

    uint8_t pkt[188] = {0x47, (pid >> 8) & 0x1f, pid & 0xff, 0x10};
    h->current_res.pid = pid;
    h->current_res.payload_len = 184;

    /* 1. Fill EB manually to simulate backpressure
     * EB Video Size is 512KB = 4,194,304 bits */
    es->tstd.eb_fill_q64 = INT_TO_Q64_64(4194304);

    /* 2. Inject a burst of 100 packets into TB */
    for (int i = 0; i < 100; i++) {
        tsa_process_packet(h, pkt, now);
    }

    uint64_t tb_initial = (uint64_t)(es->tstd.tb_fill_q64 >> 64);
    printf("Initial TB fill: %lu bits\n", tb_initial);
    assert(tb_initial >= 150400);

    /* 3. Advance time by 10ms (10,000,000 ns)
     * Rx = 1.2 * 10M = 12 Mbps = 12 bits/us = 120,000 bits / 10ms
     * Rbx = 1.5 * 10M = 15 Mbps (BUT EB is FULL, so flow to EB should be 0)
     * Therefore, all 120,000 bits from TB should now stay in MB! */
    now += 10000000;
    h->stc_ns = now;
    tsa_process_packet(h, pkt, now);  // Trigger leak

    uint64_t mb_val = (uint64_t)(es->tstd.mb_fill_q64 >> 64);
    printf("MB fill after 10ms with Backpressure: %lu bits (Expected ~120000)\n", mb_val);

    /* Assert MB is accumulation data due to EB being full */
    assert(mb_val > 110000 && mb_val < 130000);

    tsa_destroy(h);
    printf("test_tstd_mb_flow_backpressure PASSED\n");
}

int main() {
    test_tstd_mb_flow_backpressure();
    return 0;
}
