#include "tsa_packet_pool.h"

#include <stdatomic.h>
#include <stdlib.h>

tsa_packet_pool_t* tsa_packet_pool_create(size_t capacity) {
    tsa_packet_pool_t* pool = calloc(1, sizeof(tsa_packet_pool_t));
    pool->capacity = capacity;
    pool->packets = calloc(capacity, sizeof(tsa_packet_t));
    pool->free_queue = mpmc_queue_create(capacity);

    for (size_t i = 0; i < capacity; ++i) {
        mpmc_queue_push(pool->free_queue, (uint32_t)i);
    }
    return pool;
}

void tsa_packet_pool_destroy(tsa_packet_pool_t* pool) {
    if (!pool) return;
    free(pool->packets);
    mpmc_queue_destroy(pool->free_queue);
    free(pool);
}

tsa_packet_t* tsa_packet_pool_acquire(tsa_packet_pool_t* pool) {
    uint32_t idx;
    if (mpmc_queue_pop(pool->free_queue, &idx)) {
        tsa_packet_t* pkt = &pool->packets[idx];
        pkt->ref_count = 1;
        pkt->metadata = NULL;
        return pkt;
    }
    return NULL;
}

void tsa_packet_ref(tsa_packet_t* pkt) {
    if (pkt) {
        __atomic_fetch_add(&pkt->ref_count, 1, __ATOMIC_SEQ_CST);
    }
}

void tsa_packet_unref(tsa_packet_pool_t* pool, tsa_packet_t* pkt) {
    if (pkt) {
        if (__atomic_sub_fetch(&pkt->ref_count, 1, __ATOMIC_SEQ_CST) == 0) {
            uint32_t idx = (uint32_t)(pkt - pool->packets);
            mpmc_queue_push(pool->free_queue, idx);
        }
    }
}