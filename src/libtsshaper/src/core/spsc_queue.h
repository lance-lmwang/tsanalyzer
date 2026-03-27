#ifndef SPSC_QUEUE_H
#define SPSC_QUEUE_H

#include <stdalign.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define TS_PACKET_SIZE 188

/**
 * @brief Internal TS packet structure with metadata.
 * Optimized for AVX2 copies (192 bytes for data) and cache line alignment.
 */
typedef struct {
    alignas(256) uint8_t data[192];  // 188 (TS) + 4 (Padding for AVX2)
    uint16_t pid;
    uint64_t arrival_ns;
    uint8_t reserved[54];  // Pad to 256 bytes total size
} ts_packet_t;

typedef struct {
    alignas(128) _Atomic size_t head;
    alignas(128) _Atomic size_t tail;

    // Queue metadata (read-only after initialization)
    ts_packet_t* buffer;
    size_t size;
    size_t mask;
} spsc_queue_t;

spsc_queue_t* spsc_queue_create(size_t size);
void spsc_queue_free(spsc_queue_t* q);
bool spsc_queue_push(spsc_queue_t* q, const ts_packet_t* pkt);
bool spsc_queue_pop(spsc_queue_t* q, ts_packet_t* pkt);
bool spsc_queue_peek(spsc_queue_t* q, ts_packet_t* pkt);
size_t spsc_queue_count(spsc_queue_t* q);
bool spsc_queue_is_empty(spsc_queue_t* q);
bool spsc_queue_is_full(spsc_queue_t* q);

#endif  // SPSC_QUEUE_H
