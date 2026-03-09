#ifndef TSA_BITSTREAM_H
#define TSA_BITSTREAM_H

#include <stdbool.h>
#include <stdint.h>

#include "tsa_units.h"

typedef struct {
    const uint8_t* buf;
    const uint8_t* buf_end;
    uint64_t cache;
    int bits_left;
} bit_reader_t;

static inline void br_refill(bit_reader_t* r) {
    while (r->bits_left <= 56 && r->buf < r->buf_end) {
        r->cache = (r->cache << 8) | *(r->buf++);
        r->bits_left += 8;
    }
}

static inline void br_init(bit_reader_t* r, const uint8_t* buf, int size) {
    r->buf = buf;
    r->buf_end = buf + size;
    r->cache = 0;
    r->bits_left = 0;
    br_refill(r);
}

static inline uint32_t br_peek(bit_reader_t* r, int n) {
    if (n == 0 || r->bits_left < n) return 0;
    return (r->cache >> (r->bits_left - n)) & ((1ULL << n) - 1);
}

static inline void br_skip(bit_reader_t* r, int n) {
    r->bits_left -= n;
    if (r->bits_left < 0) r->bits_left = 0;
    br_refill(r);
}

static inline uint32_t br_read(bit_reader_t* r, int n) {
    if (n == 0 || r->bits_left < n) return 0;
    uint32_t val = (r->cache >> (r->bits_left - n)) & ((1ULL << n) - 1);
    r->bits_left -= n;
    br_refill(r);
    return val;
}

static inline uint32_t br_read_ue(bit_reader_t* r) {
    int leading_zero_bits = 0;
    while (r->bits_left > 0 && br_peek(r, 1) == 0 && leading_zero_bits < 32) {
        br_skip(r, 1);
        leading_zero_bits++;
    }
    if (r->bits_left > 0) br_skip(r, 1);
    return (1 << leading_zero_bits) - 1 + br_read(r, leading_zero_bits);
}

static inline int32_t br_read_se(bit_reader_t* r) {
    uint32_t ue = br_read_ue(r);
    if (ue % 2 == 0) {
        return -(int32_t)(ue / 2);
    } else {
        return (int32_t)((ue + 1) / 2);
    }
}

/**
 * Standardized PCR extraction from a 188-byte TS packet.
 * Returns the full 42-bit PCR value (Base * 300 + Extension) in 27MHz units.
 * Returns INVALID_PCR if no PCR is present or packet is invalid.
 */
#ifndef INVALID_PCR
#define INVALID_PCR ((uint64_t) - 1)
#endif

static inline uint64_t tsa_pkt_get_pcr(const uint8_t* pkt) {
    /* Basic TS Sync Check */
    if (pkt[0] != 0x47) return INVALID_PCR;

    /* Check for adaptation field existence and length (at least 2 bytes for flags) */
    if (!(pkt[3] & 0x20) || pkt[4] < 2) return INVALID_PCR;

    /* Check PCR_flag */
    if (!(pkt[5] & 0x10)) return INVALID_PCR;

    /* PCR is at offset 6: 33-bit base, 6-bit reserved, 9-bit extension */
    uint64_t base = ((uint64_t)pkt[6] << 25) | ((uint64_t)pkt[7] << 17) | ((uint64_t)pkt[8] << 9) |
                    ((uint64_t)pkt[9] << 1) | ((uint64_t)pkt[10] >> 7);
    uint64_t ext = ((uint64_t)(pkt[10] & 0x01) << 8) | (uint64_t)pkt[11];

    return (base * 300) + ext;
}

/**
 * Converts PCR ticks (27MHz) to nanoseconds (1GHz).
 * Uses 128-bit intermediate math or careful ordering to prevent overflow.
 * Formula: (ticks * 1000) / 27
 */
static inline uint64_t tsa_pcr_to_ns(uint64_t pcr_ticks) {
    if (pcr_ticks == INVALID_PCR) return 0;
    return (pcr_ticks * 1000) / 27;
}

#endif
