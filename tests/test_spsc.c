#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>

#include "tsa_internal.h"
#include "tsp.h"

#define RING_SIZE 1024
#define MSG_COUNT 1000000

static spsc_ring_t* ring;

void* producer(void* arg) {
    (void)arg;
    for (uint64_t i = 0; i < MSG_COUNT; i++) {
        while (spsc_ring_push(ring, (uint8_t*)&i, sizeof(uint64_t)) != 0) {
            // Spin
        }
    }
    return NULL;
}

void* consumer(void* arg) {
    (void)arg;
    uint64_t val;
    for (uint64_t i = 0; i < MSG_COUNT; i++) {
        while (spsc_ring_pop(ring, (uint8_t*)&val, sizeof(uint64_t)) != 0) {
            // Spin
        }
        if (val != i) {
            printf("Error: Expected %lu, got %lu\n", i, val);
            assert(val == i);
        }
    }
    return NULL;
}

int main() {
    printf("Testing SPSC Ring Buffer Concurrency...\n");

    ring = spsc_ring_create(RING_SIZE);
    assert(ring != NULL);

    pthread_t p, c;
    pthread_create(&p, NULL, producer, NULL);
    pthread_create(&c, NULL, consumer, NULL);

    pthread_join(p, NULL);
    pthread_join(c, NULL);

    spsc_ring_destroy(ring);
    printf("SPSC Ring Buffer test passed!\n");
    return 0;
}
