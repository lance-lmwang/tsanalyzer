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

/* Internal implementation for batch extraction.
   Loads 8 packet headers using gather and extracts 13-bit PIDs. */
void tsa_simd_extract_pids_8_avx2(const uint8_t* buf, uint16_t* pids) {
    /* Multipliers for packet offsets (0, 188, 376, ...) */
    __m256i offsets = _mm256_set_epi32(1316, 1128, 940, 752, 564, 376, 188, 0);

    /* Gather the first 4 bytes of each of the 8 packets */
    /* Note: We load as int32, which includes Sync, PUSI, PID, and partial AF/SC */
    __m256i headers = _mm256_i32gather_epi32((const int*)buf, offsets, 1);

    /* Byte order in TS packet header (big-endian):
       Byte 0: 0x47 (Sync)
       Byte 1: [7:5] Flags, [4:0] PID High
       Byte 2: [7:0] PID Low

       In x86 (little-endian) after gathering 4-byte dword:
       Low bits (Byte 0): 0x47
       Mid bits (Byte 1): [PUSI][PID High]
       High bits (Byte 2): [PID Low]
    */

    /* Shift right by 8 to drop sync byte */
    __m256i h_shifted = _mm256_srli_epi32(headers, 8);

    /* Now Byte 0 is [PUSI][PID High], Byte 1 is [PID Low]
       We need to swap these to get 13-bit PID in correct order for integer math,
       or just mask and shift.
       PID = ((Byte 0 & 0x1F) << 8) | Byte 1
    */

    /* Extract PID bits using mask and shuffle/shift */
    /* Mask: 0x0000FFFF (we only care about the 2 bytes now at the bottom) */
    __m256i mask_val = _mm256_set1_epi32(0xFFFF);
    __m256i raw_pid_bytes = _mm256_and_si256(h_shifted, mask_val);

    /* Final extraction per element */
    uint32_t temp[8];
    _mm256_storeu_si256((__m256i*)temp, raw_pid_bytes);

    for (int i = 0; i < 8; i++) {
        uint16_t b1 = (temp[i] & 0xFF);
        uint16_t b2 = (temp[i] >> 8) & 0xFF;
        pids[i] = ((b1 & 0x1F) << 8) | b2;
    }
}
#endif
