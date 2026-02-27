#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "tsa.h"
#include "tsa_internal.h"

#define ITERATIONS 100000

int compare_uint64(const void* a, const void* b) {
    uint64_t arg1 = *(const uint64_t*)a;
    uint64_t arg2 = *(const uint64_t*)b;
    if (arg1 < arg2) return -1;
    if (arg1 > arg2) return 1;
    return 0;
}

void test_latency_p99() {
    printf("Running test_latency_p99 (%d iterations)...\n", ITERATIONS);
    tsa_config_t cfg = {0};
    tsa_handle_t* h = tsa_create(&cfg);
    assert(h != NULL);

    uint64_t* measurements = malloc(sizeof(uint64_t) * ITERATIONS);
    uint8_t pkt[188] = {TS_SYNC_BYTE, 0x01, 0x00, 0x10};

    for (int i = 0; i < ITERATIONS; i++) {
        struct timespec start, end;
        clock_gettime(CLOCK_MONOTONIC, &start);

        tsa_process_packet(h, pkt, 1000 + i);

        clock_gettime(CLOCK_MONOTONIC, &end);

        uint64_t diff = (end.tv_sec - start.tv_sec) * 1000000000ULL + (end.tv_nsec - start.tv_nsec);
        measurements[i] = diff;
    }

    qsort(measurements, ITERATIONS, sizeof(uint64_t), compare_uint64);

    uint64_t p50 = measurements[ITERATIONS / 2];
    uint64_t p90 = measurements[(ITERATIONS * 90) / 100];
    uint64_t p99 = measurements[(ITERATIONS * 99) / 100];
    uint64_t max = measurements[ITERATIONS - 1];

    printf("Latency Distribution (ns):\n");
    printf("  P50: %lu\n", p50);
    printf("  P90: %lu\n", p90);
    printf("  P99: %lu\n", p99);
    printf("  Max: %lu\n", max);

    // Target: P99 < 2000ns (2us)
    assert(p99 < 2000);

    free(measurements);
    tsa_destroy(h);
    printf("test_latency_p99 passed.\n");
}

int main() {
    test_latency_p99();
    return 0;
}
