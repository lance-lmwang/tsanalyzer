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

#include "../src/tsp.c"
#include "tsp.h"

void test_pcr_mapping() {
    tsp_handle_t h;
    memset(&h, 0, sizeof(h));

    h.pcr_base = 0;
    h.sys_time_base = 1000000000ULL;
    h.last_pcr_val_tx = 0;
    h.byte_offset_base = 0;

    printf("Test 1: Exactly 1 second worth of PCR ticks...\n");
    uint64_t target = calculate_target_time(&h, 27000000ULL, 0, 1000000000ULL);
    printf("  Target: %lu ns, Expected: 2000000000 ns\n", target);
    assert(target == 2000000000ULL);

    printf("Test 2: Interpolation based on detected bitrate...\n");
    h.cfg.ts_per_udp = 1;
    // Set detected bitrate to 8Mbps (approx 1,000,000 bytes/sec)
    // At 8Mbps, 500,000 bytes should take 0.5s (500,000,000 ns)
    uint64_t br = 8000000ULL;
    atomic_store(&h.detected_bitrate, br);

    // slot_n = packets sent. 500,000 bytes = 500,000 / 188 packets.
    uint64_t slot_n = 500000 / 188;
    target = calculate_target_time(&h, INVALID_PCR, slot_n, 1000000000ULL);
    printf("  Interpolated Target: %lu ns, Expected ~1500000000 ns\n", target);
    // (500000 * 8 * 1e9) / 8000000 = 500,000,000 ns
    assert(target > 1490000000ULL && target < 1510000000ULL);

    printf("PCR mapping tests passed!\n");
}

int main() {
    test_pcr_mapping();
    return 0;
}
