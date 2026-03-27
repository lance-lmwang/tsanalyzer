#include "spsc_queue.h"

#include <stdlib.h>
#include <string.h>

#include "internal.h"

spsc_queue_t* spsc_queue_create(size_t size) {
    if (size == 0) return NULL;
    // Round up to power of 2
    size_t real_size = 1;
    while (real_size < size) real_size <<= 1;

    // Use aligned_alloc for the struct itself (C11) to respect member alignment
    spsc_queue_t* q = (spsc_queue_t*)aligned_alloc(128, sizeof(spsc_queue_t));
    if (!q) return NULL;
    memset(q, 0, sizeof(spsc_queue_t));

    // Use aligned_alloc for the buffer (C11)
    // 256-byte alignment matches ts_packet_t and is ideal for AVX-512 too.
    q->buffer = (ts_packet_t*)aligned_alloc(256, real_size * sizeof(ts_packet_t));
    if (!q->buffer) {
        free(q);
        return NULL;
    }

    q->size = real_size;
    q->mask = real_size - 1;
    atomic_init(&q->head, 0);
    atomic_init(&q->tail, 0);
    return q;
}

void spsc_queue_free(spsc_queue_t* q) {
    if (!q) return;
    free(q->buffer);
    free(q);
}

// Alias for compatibility with core
void spsc_queue_destroy(spsc_queue_t* q) {
    spsc_queue_free(q);
}

bool spsc_queue_push(spsc_queue_t* q, const ts_packet_t* pkt) {
    // Producer: Relaxed load of head (local), acquire load of tail (remote)
    size_t head = atomic_load_explicit(&q->head, memory_order_relaxed);
    size_t tail = atomic_load_explicit(&q->tail, memory_order_acquire);

    if ((head - tail) >= q->size) {
        return false;  // Full
    }

    // Modern compilers will optimize this 256-byte memcpy to AVX2/AVX-512
    memcpy(&q->buffer[head & q->mask], pkt, sizeof(ts_packet_t));

    // Store head with release semantics to ensure consumer sees buffer data
    atomic_store_explicit(&q->head, head + 1, memory_order_release);
    return true;
}

bool spsc_queue_pop(spsc_queue_t* q, ts_packet_t* pkt) {
    // Consumer: Relaxed load of tail (local), acquire load of head (remote)
    size_t tail = atomic_load_explicit(&q->tail, memory_order_relaxed);
    size_t head = atomic_load_explicit(&q->head, memory_order_acquire);

    if (tail == head) {
        return false;  // Empty
    }

    memcpy(pkt, &q->buffer[tail & q->mask], sizeof(ts_packet_t));

    // Store tail with release semantics to signal producer that space is free
    atomic_store_explicit(&q->tail, tail + 1, memory_order_release);
    return true;
}

bool spsc_queue_peek(spsc_queue_t* q, ts_packet_t* pkt) {
    size_t tail = atomic_load_explicit(&q->tail, memory_order_relaxed);
    size_t head = atomic_load_explicit(&q->head, memory_order_acquire);

    if (tail == head) {
        return false;  // Empty
    }

    memcpy(pkt, &q->buffer[tail & q->mask], sizeof(ts_packet_t));
    return true;
}

size_t spsc_queue_count(spsc_queue_t* q) {
    size_t head = atomic_load_explicit(&q->head, memory_order_acquire);
    size_t tail = atomic_load_explicit(&q->tail, memory_order_acquire);
    return (head - tail);
}

bool spsc_queue_is_empty(spsc_queue_t* q) {
    size_t head = atomic_load_explicit(&q->head, memory_order_acquire);
    size_t tail = atomic_load_explicit(&q->tail, memory_order_acquire);
    return (head == tail);
}
