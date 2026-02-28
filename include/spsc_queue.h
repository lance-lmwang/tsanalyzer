#ifndef SPSC_QUEUE_H
#define SPSC_QUEUE_H

#include <stdalign.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/**
 * SPSC (Single-Producer Single-Consumer) lock-free ring buffer.
 * Designed for high-performance packet passing in the TsAnalyzer pipeline.
 * Uses 64-byte alignment to prevent false sharing between producer and consumer.
 */

typedef struct {
    uint64_t timestamp_ns;
    uint8_t data[188];
} ts_packet_t;

typedef struct {
    alignas(64) _Atomic size_t head;
    alignas(64) _Atomic size_t tail;
    size_t size;
    ts_packet_t* buffer;
} spsc_queue_t;

spsc_queue_t* spsc_queue_create(size_t size);
void spsc_queue_destroy(spsc_queue_t* q);

bool spsc_queue_push(spsc_queue_t* q, const ts_packet_t* pkt);
bool spsc_queue_pop(spsc_queue_t* q, ts_packet_t* pkt);

size_t spsc_queue_count(spsc_queue_t* q);
bool spsc_queue_is_full(spsc_queue_t* q);
bool spsc_queue_is_empty(spsc_queue_t* q);

#endif
