#ifndef SPSC_QUEUE_H
#define SPSC_QUEUE_H

#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define TS_PACKET_SIZE 188

/**
 * @brief Internal TS packet structure with metadata.
 */
typedef struct {
    uint8_t data[TS_PACKET_SIZE];
    uint16_t pid;
    uint64_t arrival_ns;
} ts_packet_t;

typedef struct {
    ts_packet_t* buffer;
    size_t size;
    size_t mask;
    _Atomic size_t head;
    _Atomic size_t tail;
} spsc_queue_t;

spsc_queue_t* spsc_queue_create(size_t size);
void spsc_queue_free(spsc_queue_t* q);
bool spsc_queue_push(spsc_queue_t* q, const ts_packet_t* pkt);
bool spsc_queue_pop(spsc_queue_t* q, ts_packet_t* pkt);
size_t spsc_queue_count(spsc_queue_t* q);
bool spsc_queue_is_empty(spsc_queue_t* q);
bool spsc_queue_is_full(spsc_queue_t* q);

#endif  // SPSC_QUEUE_H
