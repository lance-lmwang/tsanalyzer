#include "tsa_simd.h"

#include <stdio.h>

#if defined(__x86_64__) || defined(_M_X64)
#include <cpuid.h>

/* Internal declaration of the AVX2 implementation */
extern intptr_t tsa_simd_find_sync_avx2(const uint8_t* buf, size_t len);

bool tsa_simd_capable(void) {
    static int cached_result = -1;
    if (cached_result != -1) return (bool)cached_result;

    unsigned int eax, ebx, ecx, edx;
    if (__get_cpuid(1, &eax, &ebx, &ecx, &edx)) {
        /* Check bit 28 of ECX for OSXSAVE, required for AVX */
        if (ecx & (1 << 28)) {
            unsigned int eax7, ebx7, ecx7, edx7;
            if (__get_cpuid_count(7, 0, &eax7, &ebx7, &ecx7, &edx7)) {
                cached_result = (ebx7 & (1 << 5)) != 0; /* AVX2 bit */
                return (bool)cached_result;
            }
        }
    }
    cached_result = 0;
    return false;
}

intptr_t tsa_simd_find_sync(const uint8_t* buf, size_t len) {
    if (tsa_simd_capable()) {
        return tsa_simd_find_sync_avx2(buf, len);
    }

    /* Fallback: Standard Scalar C */
    for (size_t i = 0; i < len; i++) {
        if (buf[i] == 0x47) return (intptr_t)i;
    }
    return -1;
}

#else
/* Non-x86 architectures */
bool tsa_simd_capable(void) {
    return false;
}
intptr_t tsa_simd_find_sync(const uint8_t* buf, size_t len) {
    for (size_t i = 0; i < len; i++) {
        if (buf[i] == 0x47) return (intptr_t)i;
    }
    return -1;
}
#endif

uint64_t tsa_simd_check_sync_batch(const uint8_t* buf, size_t packet_count) {
    if (packet_count > 64) packet_count = 64;
    uint64_t mask = 0;
    for (size_t i = 0; i < packet_count; i++) {
        if (buf[i * 188] == 0x47) mask |= (1ULL << i);
    }
    return mask;
}
