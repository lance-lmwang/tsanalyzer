#include "tsa_simd.h"

#include <pthread.h>
#include <stdio.h>

/* --- Default Scalar Implementations --- */

static intptr_t find_sync_scalar(const uint8_t* buf, size_t len) {
    for (size_t i = 0; i < len; i++) {
        if (buf[i] == 0x47) return (intptr_t)i;
    }
    return -1;
}

static void extract_pids_8_scalar(const uint8_t* buf, uint16_t* pids) {
    for (int i = 0; i < 8; i++) {
        const uint8_t* p = buf + (i * 188);
        pids[i] = ((p[1] & 0x1f) << 8) | p[2];
    }
}

static uint64_t check_sync_batch_scalar(const uint8_t* buf, size_t packet_count) {
    if (packet_count > 64) packet_count = 64;
    uint64_t mask = 0;
    for (size_t i = 0; i < packet_count; i++) {
        if (buf[i * 188] == 0x47) mask |= (1ULL << i);
    }
    return mask;
}

/* --- Global Dispatch Table --- */

tsa_simd_ops_t tsa_simd = {.find_sync = find_sync_scalar,
                           .extract_pids_8 = extract_pids_8_scalar,
                           .check_sync_batch = check_sync_batch_scalar,
                           .is_accelerated = false};

/* --- Runtime Detection (x86 only) --- */

#if defined(__x86_64__) || defined(_M_X64)
#include <cpuid.h>

extern intptr_t tsa_simd_find_sync_avx2(const uint8_t* buf, size_t len);
extern void tsa_simd_extract_pids_8_avx2(const uint8_t* buf, uint16_t* pids);

bool tsa_simd_capable(void) {
    static int cached_result = -1;
    if (cached_result != -1) return (bool)cached_result;

    unsigned int eax, ebx, ecx, edx;
    if (__get_cpuid(1, &eax, &ebx, &ecx, &edx)) {
        if (ecx & (1 << 28)) { /* OSXSAVE */
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

static void tsa_simd_init_impl(void) {
    if (tsa_simd_capable()) {
        tsa_simd.find_sync = tsa_simd_find_sync_avx2;
        tsa_simd.extract_pids_8 = tsa_simd_extract_pids_8_avx2;
        tsa_simd.is_accelerated = true;
    }
}
#else
bool tsa_simd_capable(void) {
    return false;
}
static void tsa_simd_init_impl(void) {
}
#endif

void tsa_simd_init(void) {
    static pthread_once_t once = PTHREAD_ONCE_INIT;
    pthread_once(&once, tsa_simd_init_impl);
}

/* Auto-initialize on first use if not already done */
__attribute__((constructor)) static void tsa_simd_auto_init(void) {
    tsa_simd_init();
}

/* --- Compatibility Wrappers --- */

intptr_t tsa_simd_find_sync(const uint8_t* buf, size_t len) {
    return tsa_simd.find_sync(buf, len);
}

void tsa_simd_extract_pids_8(const uint8_t* buf, uint16_t* pids) {
    tsa_simd.extract_pids_8(buf, pids);
}

uint64_t tsa_simd_check_sync_batch(const uint8_t* buf, size_t packet_count) {
    return tsa_simd.check_sync_batch(buf, packet_count);
}
