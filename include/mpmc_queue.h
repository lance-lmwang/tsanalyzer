#ifndef MPMC_QUEUE_H
#define MPMC_QUEUE_H

#include <stdalign.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    size_t capacity;
    alignas(64) _Atomic size_t head;
    alignas(64) _Atomic size_t tail;
    _Atomic uint32_t* buffer;
} mpmc_queue_t;

mpmc_queue_t* mpmc_queue_create(size_t capacity);
void mpmc_queue_destroy(mpmc_queue_t* q);
bool mpmc_queue_push(mpmc_queue_t* q, uint32_t item);
bool mpmc_queue_pop(mpmc_queue_t* q, uint32_t* item);

#endif
