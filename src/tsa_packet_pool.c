#include <stdlib.h>
#include <stdatomic.h>
#include "tsa_packet_pool.h"

tsa_packet_pool_t* tsa_packet_pool_create(size_t capacity) {
    tsa_packet_pool_t* pool = calloc(1, sizeof(tsa_packet_pool_t));
    pool->capacity = capacity;
    pool->packets = calloc(capacity, sizeof(tsa_packet_t));
    pthread_mutex_init(&pool->lock, NULL);
    
    // Simple ring buffer logic for free list
    pool->head = 0;
    pool->tail = capacity - 1; // Initially full
    return pool;
}

void tsa_packet_pool_destroy(tsa_packet_pool_t* pool) {
    if (!pool) return;
    free(pool->packets);
    pthread_mutex_destroy(&pool->lock);
    free(pool);
}

tsa_packet_t* tsa_packet_pool_acquire(tsa_packet_pool_t* pool) {
    tsa_packet_t* pkt = NULL;
    pthread_mutex_lock(&pool->lock);
    if (pool->head != pool->tail) {
        pkt = &pool->packets[pool->head];
        pool->head = (pool->head + 1) % pool->capacity;
    }
    pthread_mutex_unlock(&pool->lock);
    
    if (pkt) {
        pkt->ref_count = 1;
    }
    return pkt;
}

void tsa_packet_ref(tsa_packet_t* pkt) {
    if (pkt) {
        // Simple atomic increment for thread safety
        __atomic_fetch_add(&pkt->ref_count, 1, __ATOMIC_SEQ_CST);
    }
}

void tsa_packet_unref(tsa_packet_pool_t* pool, tsa_packet_t* pkt) {
    if (pkt) {
        if (__atomic_sub_fetch(&pkt->ref_count, 1, __ATOMIC_SEQ_CST) == 0) {
            pthread_mutex_lock(&pool->lock);
            // Return to pool (we push it back to the tail)
            pool->tail = (pool->tail + 1) % pool->capacity;
            // The actual memory pointer arithmetic could be optimized in a lock-free way,
            // but we'll use a mutex for the initial Phase 2 baseline.
            pthread_mutex_unlock(&pool->lock);
        }
    }
}
