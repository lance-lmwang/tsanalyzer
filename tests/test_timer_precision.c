#define _GNU_SOURCE
#include <errno.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#define NANOSECONDS_PER_SECOND 1000000000ULL
#define ITERATIONS 1000
#define SLEEP_NS 1000000ULL  // 1ms

static uint64_t get_time_ns(clockid_t clk_id) {
    struct timespec ts;
    clock_gettime(clk_id, &ts);
    return (uint64_t)ts.tv_sec * NANOSECONDS_PER_SECOND + ts.tv_nsec;
}

int main() {
    clockid_t clk_id = CLOCK_MONOTONIC;
    uint64_t start_time = get_time_ns(clk_id);
    uint64_t sum_abs_diff = 0;
    int64_t max_diff = -2000000000LL;
    int64_t min_diff = 2000000000LL;

    printf("Starting timer precision test: %d iterations of %llu ns sleep\n", ITERATIONS, SLEEP_NS);

    for (int i = 0; i < ITERATIONS; i++) {
        uint64_t target_ns = start_time + (uint64_t)(i + 1) * SLEEP_NS;
        struct timespec ts_target;
        ts_target.tv_sec = target_ns / NANOSECONDS_PER_SECOND;
        ts_target.tv_nsec = target_ns % NANOSECONDS_PER_SECOND;

        while (clock_nanosleep(clk_id, TIMER_ABSTIME, &ts_target, NULL) != 0) {
            if (errno != EINTR) break;
        }

        uint64_t now = get_time_ns(clk_id);
        int64_t diff = (int64_t)now - (int64_t)target_ns;

        sum_abs_diff += abs((int)diff);  // Using abs from stdlib.h for int64 is tricky, but here diff is small
        if (diff > max_diff) max_diff = diff;
        if (diff < min_diff) min_diff = diff;
    }

    printf("Results (nanoseconds):\n");
    printf("  Max Deviation: %ld ns\n", max_diff);
    printf("  Min Deviation: %ld ns\n", min_diff);
    printf("  Avg Abs Deviation: %.2f ns\n", (double)sum_abs_diff / ITERATIONS);

    if (sum_abs_diff / ITERATIONS > 500000) {
        printf("WARNING: Timer precision is poor (> 500us avg deviation).\n");
    } else {
        printf("Timer precision looks acceptable for general pacing.\n");
    }

    return 0;
}
