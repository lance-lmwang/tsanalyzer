#ifndef TSA_PACKET_POOL_H
#define TSA_PACKET_POOL_H

#include <pthread.h>
#include <stddef.h>

#include "tsa_pipeline.h"

typedef struct {
    tsa_packet_t* packets;
    size_t capacity;
    size_t head;
    size_t tail;
    pthread_mutex_t lock;
} tsa_packet_pool_t;

tsa_packet_pool_t* tsa_packet_pool_create(size_t capacity);
void tsa_packet_pool_destroy(tsa_packet_pool_t* pool);

/**
 * Acquire a free packet from the pool. Returns NULL if pool is empty.
 * The ref_count is initialized to 1.
 */
tsa_packet_t* tsa_packet_pool_acquire(tsa_packet_pool_t* pool);

/**
 * Increment the reference count of the packet.
 */
void tsa_packet_ref(tsa_packet_t* pkt);

/**
 * Decrement the reference count. If it reaches 0, it is returned to the pool.
 */
void tsa_packet_unref(tsa_packet_pool_t* pool, tsa_packet_t* pkt);

#endif
