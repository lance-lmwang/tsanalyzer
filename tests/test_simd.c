#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdbool.h>

#include "tsa_simd.h"

/* Manually declare internal versions for benchmarking */
extern intptr_t tsa_simd_find_sync_avx2(const uint8_t* buf, size_t len);
extern intptr_t tsa_simd_find_sync_sse42(const uint8_t* buf, size_t len);

static double get_time() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}

static intptr_t find_sync_scalar(const uint8_t* buf, size_t len) {
    for (size_t i = 0; i < len; i++) {
        if (buf[i] == 0x47) return (intptr_t)i;
    }
    return -1;
}

void benchmark() {
    size_t size = 100 * 1024 * 1024;  // 100MB
    uint8_t* data = malloc(size);
    if (!data) return;
    memset(data, 0, size);
    data[size - 1] = 0x47;  // Put sync at the very end

    printf("\n=== Performance Benchmark (100MB Buffer) ===\n");

    double start = get_time();
    intptr_t res = find_sync_scalar(data, size);
    printf("Scalar Implementation: %.4f seconds (res=%ld)\n", get_time() - start, (long)res);

    /* Use simple env-based checks or just rely on the dispatch table for safety */
    if (tsa_simd.is_accelerated) {
        printf("SIMD Acceleration:     ENABLED\n");
        start = get_time();
        res = tsa_simd_find_sync(data, size);
        printf("Active SIMD Impl:      %.4f seconds (res=%ld)\n", get_time() - start, (long)res);
    } else {
        printf("SIMD Acceleration:     DISABLED (CPU not capable or fallback used)\n");
    }

    free(data);
}

int main() {
    printf("Starting TSA SIMD validation...\n");

    size_t buf_len = 1024;
    uint8_t* buf = malloc(buf_len);
    memset(buf, 0, buf_len);

    // Test 1: Sync byte at the beginning
    buf[0] = 0x47;
    assert(tsa_simd_find_sync(buf, buf_len) == 0);

    // Test 2: Sync byte in the middle
    memset(buf, 0, buf_len);
    buf[513] = 0x47;
    assert(tsa_simd_find_sync(buf, buf_len) == 513);

    // Test 3: Multiple sync bytes, find first
    memset(buf, 0, buf_len);
    buf[100] = 0x47;
    buf[200] = 0x47;
    assert(tsa_simd_find_sync(buf, buf_len) == 100);

    // Test 4: No sync byte
    memset(buf, 0, buf_len);
    assert(tsa_simd_find_sync(buf, buf_len) == -1);

    // Test 5: Batch sync check
    uint8_t batch_buf[188 * 10];
    memset(batch_buf, 0, sizeof(batch_buf));
    batch_buf[0] = 0x47;
    batch_buf[188 * 2] = 0x47;
    batch_buf[188 * 5] = 0x47;

    uint64_t mask = tsa_simd_check_sync_batch(batch_buf, 10);
    assert(mask & (1ULL << 0));
    assert(!(mask & (1ULL << 1)));
    assert(mask & (1ULL << 2));
    assert(mask & (1ULL << 5));

    // Test 6: Batch PID extraction
    uint8_t pid_buf[188 * 8];
    uint16_t expected_pids[8] = {0x123, 0x1FFF, 0x000, 0x0100, 0x0456, 0x1ABC, 0x0011, 0x0000};
    memset(pid_buf, 0, sizeof(pid_buf));
    for (int i = 0; i < 8; i++) {
        pid_buf[i * 188] = 0x47;
        pid_buf[i * 188 + 1] = (expected_pids[i] >> 8) & 0x1F;
        pid_buf[i * 188 + 2] = expected_pids[i] & 0xFF;
    }
    uint16_t extracted_pids[8];
    tsa_simd_extract_pids_8(pid_buf, extracted_pids);
    for (int i = 0; i < 8; i++) {
        assert(extracted_pids[i] == expected_pids[i]);
    }

    free(buf);
    printf("SIMD functionality tests passed!\n");
    benchmark();

    return 0;
}
