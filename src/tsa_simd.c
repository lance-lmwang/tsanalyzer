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

extern intptr_t tsa_simd_find_sync_sse42(const uint8_t* buf, size_t len);
extern void tsa_simd_extract_pids_8_sse42(const uint8_t* buf, uint16_t* pids);

typedef struct {
    bool has_avx2;
    bool has_sse42;
} tsa_cpu_caps_t;

static tsa_cpu_caps_t g_cpu_caps = {0};

static tsa_cpu_caps_t tsa_get_cpu_caps(void) {
    tsa_cpu_caps_t caps = {0};
    unsigned int eax, ebx, ecx, edx;

    /* Check for SSE4.2 (Leaf 1, ECX bit 20) */
    if (__get_cpuid(1, &eax, &ebx, &ecx, &edx)) {
        caps.has_sse42 = (ecx & (1 << 20)) != 0;

        /* Check for AVX2 (Leaf 7, EBX bit 5)
         * Requires:
         * 1. CPUID Leaf 1, ECX bit 27 (OSXSAVE)
         * 2. CPUID Leaf 1, ECX bit 28 (AVX)
         * 3. XGETBV Result bit 1 & 2 (XMM & YMM state)
         */
        bool has_osxsave = (ecx & (1 << 27)) != 0;
        bool has_avx = (ecx & (1 << 28)) != 0;

        if (has_osxsave && has_avx) {
            /* Check XCR0 for AVX state enablement */
            uint32_t xcr0_eax, xcr0_edx;
            __asm__("xgetbv" : "=a"(xcr0_eax), "=d"(xcr0_edx) : "c"(0));
            if ((xcr0_eax & 0x6) == 0x6) { /* XMM (bit 1) and YMM (bit 2) */
                unsigned int eax7, ebx7, ecx7, edx7;
                if (__get_cpuid_count(7, 0, &eax7, &ebx7, &ecx7, &edx7)) {
                    caps.has_avx2 = (ebx7 & (1 << 5)) != 0;
                }
            }
        }
    }
    return caps;
}

bool tsa_simd_capable(void) {
    return g_cpu_caps.has_avx2 || g_cpu_caps.has_sse42;
}

static void tsa_simd_init_impl(void) {
    g_cpu_caps = tsa_get_cpu_caps();
    if (g_cpu_caps.has_avx2) {
        tsa_simd.find_sync = tsa_simd_find_sync_avx2;
        tsa_simd.extract_pids_8 = tsa_simd_extract_pids_8_avx2;
        tsa_simd.is_accelerated = true;
    } else if (g_cpu_caps.has_sse42) {
        tsa_simd.find_sync = tsa_simd_find_sync_sse42;
        tsa_simd.extract_pids_8 = tsa_simd_extract_pids_8_sse42;
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
