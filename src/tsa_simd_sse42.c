#include "tsa_simd.h"

#if defined(__x86_64__) || defined(_M_X64)
#include <nmmintrin.h>

/* This function is compiled with -msse4.2 via CMake source properties */
intptr_t tsa_simd_find_sync_sse42(const uint8_t* buf, size_t len) {
    if (len < 16) {
        for (size_t i = 0; i < len; i++) {
            if (buf[i] == 0x47) return (intptr_t)i;
        }
        return -1;
    }

    __m128i pattern = _mm_set1_epi8(0x47);
    size_t i = 0;

    /* Align to 16 bytes for optimized load */
    while (((uintptr_t)(buf + i) & 15) != 0 && i < len) {
        if (buf[i] == 0x47) return (intptr_t)i;
        i++;
    }

    for (; i + 15 < len; i += 16) {
        __m128i data = _mm_load_si128((const __m128i*)(buf + i));
        __m128i cmp = _mm_cmpeq_epi8(data, pattern);
        int mask = _mm_movemask_epi8(cmp);
        if (mask != 0) {
            return (intptr_t)(i + __builtin_ctz(mask));
        }
    }

    for (; i < len; i++) {
        if (buf[i] == 0x47) return (intptr_t)i;
    }

    return -1;
}

void tsa_simd_extract_pids_8_sse42(const uint8_t* buf, uint16_t* pids) {
    /* Standard scalar fallback for SSE4.2 slot as SSE doesn't have efficient gather */
    for (int i = 0; i < 8; i++) {
        const uint8_t* p = buf + (i * 188);
        pids[i] = ((p[1] & 0x1f) << 8) | p[2];
    }
}
#endif
