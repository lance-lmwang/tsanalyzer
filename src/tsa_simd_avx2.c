#include "tsa_simd.h"

#if defined(__x86_64__) || defined(_M_X64)
#include <immintrin.h>

/* This function is compiled with -mavx2.
   It MUST only be called if tsa_simd_capable() returns true. */
intptr_t tsa_simd_find_sync_avx2(const uint8_t* buf, size_t len) {
    if (len < 32) {
        for (size_t i = 0; i < len; i++) {
            if (buf[i] == 0x47) return (intptr_t)i;
        }
        return -1;
    }

    __m256i pattern = _mm256_set1_epi8(0x47);
    size_t i = 0;

    /* Align to 32 bytes for optimized load */
    while (((uintptr_t)(buf + i) & 31) != 0 && i < len) {
        if (buf[i] == 0x47) return (intptr_t)i;
        i++;
    }

    for (; i + 31 < len; i += 32) {
        __m256i data = _mm256_load_si256((const __m256i*)(buf + i));
        __m256i cmp = _mm256_cmpeq_epi8(data, pattern);
        int mask = _mm256_movemask_epi8(cmp);
        if (mask != 0) {
            return (intptr_t)(i + __builtin_ctz(mask));
        }
    }

    for (; i < len; i++) {
        if (buf[i] == 0x47) return (intptr_t)i;
    }

    return -1;
}
#endif
