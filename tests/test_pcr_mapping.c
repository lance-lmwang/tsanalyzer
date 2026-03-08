#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <math.h>
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "tsp.h"

// Note: We are testing the logic by mocking the internal state
// Since calculate_target_time was moved to tsp_enqueue, we test the effect
// of enqueueing packets with PCR.

void test_pcr_mapping() {
    tsp_config_t cfg = {0};
    cfg.bitrate = 8000000;
    cfg.mode = TSPACER_MODE_PCR;

    tsp_handle_t* h = tsp_create(&cfg);
    assert(h != NULL);

    printf("Test 1: First PCR initialization...\n");
    uint8_t pkt[188] = {0x47, 0x01, 0x00, 0x30, 0x07, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    // PCR = 0
    tsp_enqueue(h, pkt, 1);

    assert(h->schedule_pcr_base == 0);
    assert(h->schedule_start_ns > 0);
    uint64_t start_ns = h->ts_buffer[h->tail % RING_BUFFER_SIZE];
    printf("  Start NS: %lu\n", start_ns);

    printf("Test 2: Exactly 1 second worth of PCR ticks...\n");
    // PCR = 27,000,000 (1 second)
    // Base 33-bit = 90,000. 90000 * 300 = 27,000,000
    pkt[6] = 0x00;
    pkt[7] = 0x01;
    pkt[8] = 0x5F;
    pkt[9] = 0x90;
    pkt[10] = 0x00;

    tsp_enqueue(h, pkt, 1);
    uint64_t target = h->ts_buffer[(h->tail + 1) % RING_BUFFER_SIZE];
    printf("  Target: %lu ns, Diff: %lu ns\n", target, target - start_ns);

    // Should be exactly 1 second (1,000,000,000 ns) diff
    assert(target - start_ns == 1000000000ULL);

    printf("PCR mapping tests passed!\n");
    tsp_destroy(h);
}

int main() {
    test_pcr_mapping();
    return 0;
}
