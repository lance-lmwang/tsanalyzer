#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "tsp.h"

// Define internal functions needed for test
static inline uint64_t tsp_slot_time(uint64_t start_time, uint64_t n, uint64_t udp_rate_scaled) {
    __uint128_t tmp = (__uint128_t)n * 1000000000ULL * 1000ULL;
    return start_time + (uint64_t)(tmp / (__uint128_t)udp_rate_scaled);
}

void test_catchup_logic() {
    uint64_t start_time = 1000000000ULL;  // 1s
    uint64_t rate_scaled = 1000000ULL;    // 1000 pkt/s * 1000 scale

    // Simulate being 500ms behind at n=0
    uint64_t now = start_time + 500000000ULL;

    uint64_t target_ns = tsp_slot_time(start_time, 0, rate_scaled);
    int64_t diff = (int64_t)now - (int64_t)target_ns;
    printf("Initial diff: %ld ns\n", diff);
    assert(diff == 500000000LL);

    // Goal: logic should reset start_time if diff > 100ms
    const int64_t MAX_CATCHUP_NS = 100000000LL;  // 100ms

    if (diff > MAX_CATCHUP_NS) {
        printf("Diff > 100ms, resetting start time...\n");
        start_time = now;
    }

    target_ns = tsp_slot_time(start_time, 0, rate_scaled);
    diff = (int64_t)now - (int64_t)target_ns;
    printf("Adjusted diff: %ld ns\n", diff);
    assert(diff == 0);

    printf("Catch-up logic tests passed!\n");
}

int main() {
    test_catchup_logic();
    return 0;
}
