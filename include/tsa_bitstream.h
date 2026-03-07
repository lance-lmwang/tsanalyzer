#ifndef TSA_BITSTREAM_H
#define TSA_BITSTREAM_H

#include <stdbool.h>
#include <stdint.h>

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

#endif
