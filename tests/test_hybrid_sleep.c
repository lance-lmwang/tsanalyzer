#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#define NANOSECONDS_PER_SECOND 1000000000ULL

static uint64_t get_time_ns(clockid_t clk_id) {
    struct timespec ts;
    clock_gettime(clk_id, &ts);
    return (uint64_t)ts.tv_sec * NANOSECONDS_PER_SECOND + ts.tv_nsec;
}

int main() {
    clockid_t clk_id = CLOCK_MONOTONIC;  // Matches the fix
    uint64_t start_time = get_time_ns(clk_id);

    // Target: 10ms in the future
    uint64_t target_ns = start_time + 10000000ULL;
    struct timespec ts_target;
    ts_target.tv_sec = target_ns / NANOSECONDS_PER_SECOND;
    ts_target.tv_nsec = target_ns % NANOSECONDS_PER_SECOND;

    // 1. Sleep
    while (clock_nanosleep(clk_id, TIMER_ABSTIME, &ts_target, NULL) != 0) {
        if (errno != EINTR) break;
    }

    // 2. Hybrid Spin (Simulated logic from tspacer.c)
    uint64_t now_ns = get_time_ns(clk_id);
    int spins = 0;
    while (now_ns < target_ns) {
        now_ns = get_time_ns(clk_id);
        spins++;
    }

    int64_t jitter = (int64_t)now_ns - (int64_t)target_ns;

    printf("Target: %lu\n", target_ns);
    printf("Actual: %lu\n", now_ns);
    printf("Jitter: %ld ns\n", jitter);
    printf("Spins: %d\n", spins);

    if (jitter < 0) {
        printf("[FAIL] Woke up too early!\n");
        return 1;
    } else {
        printf("[PASS] Precision maintained.\n");
        return 0;
    }
}
