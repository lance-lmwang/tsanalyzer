#include "mpmc_queue.h"

#include <stdlib.h>

mpmc_queue_t* mpmc_queue_create(size_t capacity) {
    if (capacity == 0 || (capacity & (capacity - 1)) != 0) {
        // capacity must be a power of 2 for fast modulo
        size_t power = 1;
        while (power < capacity && power != 0) power *= 2;
        capacity = power;
    }

    mpmc_queue_t* q = calloc(1, sizeof(mpmc_queue_t));
    if (!q) return NULL;

    q->capacity = capacity;
    q->buffer = calloc(capacity, sizeof(_Atomic uint32_t));
    if (!q->buffer) {
        free(q);
        return NULL;
    }

    for (size_t i = 0; i < capacity; i++) {
        atomic_init(&q->buffer[i], UINT32_MAX);
    }

    atomic_init(&q->head, 0);
    atomic_init(&q->tail, 0);

    return q;
}

void mpmc_queue_destroy(mpmc_queue_t* q) {
    if (!q) return;
    if (q->buffer) free((void*)q->buffer);
    free(q);
}

bool mpmc_queue_push(mpmc_queue_t* q, uint32_t item) {
    if (!q) return false;

    // Check if full (loose check)
    size_t head = atomic_load_explicit(&q->head, memory_order_relaxed);
    size_t tail = atomic_load_explicit(&q->tail, memory_order_relaxed);
    if (head - tail >= q->capacity) {
        return false;
    }

    size_t my_head = atomic_fetch_add_explicit(&q->head, 1, memory_order_relaxed);
    size_t idx = my_head & (q->capacity - 1);

    uint32_t expected = UINT32_MAX;
    while (!atomic_compare_exchange_weak_explicit(&q->buffer[idx], &expected, item, memory_order_release,
                                                  memory_order_relaxed)) {
        expected = UINT32_MAX;  // keep trying until slot is empty
    }
    return true;
}

bool mpmc_queue_pop(mpmc_queue_t* q, uint32_t* item) {
    if (!q || !item) return false;

    size_t tail = atomic_load_explicit(&q->tail, memory_order_relaxed);
    while (true) {
        size_t head = atomic_load_explicit(&q->head, memory_order_acquire);
        if (tail >= head) return false;  // Empty

        size_t next_tail = tail + 1;
        if (atomic_compare_exchange_weak_explicit(&q->tail, &tail, next_tail, memory_order_relaxed,
                                                  memory_order_relaxed)) {
            size_t idx = tail & (q->capacity - 1);
            uint32_t val;
            while ((val = atomic_exchange_explicit(&q->buffer[idx], UINT32_MAX, memory_order_acquire)) == UINT32_MAX) {
                // Wait for producer to write the item
            }
            *item = val;
            return true;
        }
    }
    return false;
}
