#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define SAMPLES 1000000

int compare_uint64(const void* a, const void* b) {
    uint64_t arg1 = *(const uint64_t*)a;
    uint64_t arg2 = *(const uint64_t*)b;
    if (arg1 < arg2) return -1;
    if (arg1 > arg2) return 1;
    return 0;
}

void test_jitter_burn() {
    printf("Running test_jitter_burn (%d clock samples)...\n", SAMPLES);

    uint64_t* jitter = malloc(sizeof(uint64_t) * SAMPLES);
    struct timespec last, now;

    clock_gettime(CLOCK_MONOTONIC, &last);

    for (int i = 0; i < SAMPLES; i++) {
        clock_gettime(CLOCK_MONOTONIC, &now);

        uint64_t diff = (now.tv_sec - last.tv_sec) * 1000000000ULL + (now.tv_nsec - last.tv_nsec);
        jitter[i] = diff;
        last = now;
    }

    qsort(jitter, SAMPLES, sizeof(uint64_t), compare_uint64);

    uint64_t p50 = jitter[SAMPLES / 2];
    uint64_t p99 = jitter[(SAMPLES * 99) / 100];
    uint64_t max = jitter[SAMPLES - 1];

    printf("Clock Resolution/Jitter (ns):\n");
    printf("  P50 (Min Resolution): %lu\n", p50);
    printf("  P99: %lu\n", p99);
    printf("  Max: %lu\n", max);

    // Target: P99 < 1000ns (1us)
    assert(p99 < 1000);

    free(jitter);
    printf("test_jitter_burn passed.\n");
}

int main() {
    test_jitter_burn();
    return 0;
}
