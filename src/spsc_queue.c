#include <stdlib.h>
#include <string.h>

#include "spsc_queue.h"

spsc_queue_t* spsc_queue_create(size_t size) {
    if (size == 0) return NULL;
    spsc_queue_t* q = calloc(1, sizeof(spsc_queue_t));
    if (!q) return NULL;

    q->size = size + 1;  // Need one empty slot to distinguish full/empty
    q->buffer = calloc(q->size, sizeof(ts_packet_t));
    if (!q->buffer) {
        free(q);
        return NULL;
    }

    atomic_init(&q->head, 0);
    atomic_init(&q->tail, 0);
    return q;
}

void spsc_queue_destroy(spsc_queue_t* q) {
    if (!q) return;
    free(q->buffer);
    free(q);
}

bool spsc_queue_push(spsc_queue_t* q, const ts_packet_t* pkt) {
    if (!q || !pkt) return false;

    size_t head = atomic_load_explicit(&q->head, memory_order_relaxed);
    size_t next_head = (head + 1) % q->size;
    size_t tail = atomic_load_explicit(&q->tail, memory_order_acquire);

    if (next_head == tail) {
        return false;  // Queue full
    }

    memcpy(&q->buffer[head], pkt, sizeof(ts_packet_t));
    atomic_store_explicit(&q->head, next_head, memory_order_release);
    return true;
}

bool spsc_queue_pop(spsc_queue_t* q, ts_packet_t* pkt) {
    if (!q || !pkt) return false;

    size_t tail = atomic_load_explicit(&q->tail, memory_order_relaxed);
    size_t head = atomic_load_explicit(&q->head, memory_order_acquire);

    if (tail == head) {
        return false;  // Queue empty
    }

    memcpy(pkt, &q->buffer[tail], sizeof(ts_packet_t));
    atomic_store_explicit(&q->tail, (tail + 1) % q->size, memory_order_release);
    return true;
}

size_t spsc_queue_count(spsc_queue_t* q) {
    if (!q) return 0;
    size_t head = atomic_load_explicit(&q->head, memory_order_relaxed);
    size_t tail = atomic_load_explicit(&q->tail, memory_order_relaxed);
    if (head >= tail) {
        return head - tail;
    } else {
        return q->size - (tail - head);
    }
}

bool spsc_queue_is_full(spsc_queue_t* q) {
    if (!q) return true;
    size_t head = atomic_load_explicit(&q->head, memory_order_relaxed);
    size_t tail = atomic_load_explicit(&q->tail, memory_order_relaxed);
    return ((head + 1) % q->size) == tail;
}

bool spsc_queue_is_empty(spsc_queue_t* q) {
    if (!q) return true;
    size_t head = atomic_load_explicit(&q->head, memory_order_relaxed);
    size_t tail = atomic_load_explicit(&q->tail, memory_order_relaxed);
    return head == tail;
}
