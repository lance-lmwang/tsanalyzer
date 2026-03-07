#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tsa_simd.h"

int main() {
    printf("Testing SIMD capabilities...\n");
    bool capable = tsa_simd_capable();
    printf("CPU SIMD Capable (AVX2): %s\n", capable ? "YES" : "NO");

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

    // Test 5: Unaligned access
    memset(buf, 0, buf_len);
    buf[33] = 0x47;
    assert(tsa_simd_find_sync(buf + 1, buf_len - 1) == 32);

    // Test 6: Batch sync check
    uint8_t batch_buf[188 * 10];
    memset(batch_buf, 0, sizeof(batch_buf));
    batch_buf[0] = 0x47;
    batch_buf[188 * 2] = 0x47;
    batch_buf[188 * 5] = 0x47;

    uint64_t mask = tsa_simd_check_sync_batch(batch_buf, 10);
    (void)mask;
    assert(mask & (1ULL << 0));
    assert(!(mask & (1ULL << 1)));
    assert(mask & (1ULL << 2));
    assert(mask & (1ULL << 5));

    // Test 7: Batch PID extraction
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
    printf("SIMD tests passed!\n");
    return 0;
}
