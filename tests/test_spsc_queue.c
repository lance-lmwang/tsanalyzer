#include <assert.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "spsc_queue.h"

#define QUEUE_SIZE 1024
#define MSG_COUNT 1000000

static spsc_queue_t* q;

void* producer(void* arg) {
    (void)arg;
    for (uint64_t i = 0; i < MSG_COUNT; i++) {
        ts_packet_t pkt;
        pkt.timestamp_ns = i;
        memset(pkt.data, (uint8_t)(i & 0xFF), 188);
        while (!spsc_queue_push(q, &pkt)) {
            // Spin
        }
    }
    return NULL;
}

void* consumer(void* arg) {
    (void)arg;
    ts_packet_t pkt;
    for (uint64_t i = 0; i < MSG_COUNT; i++) {
        while (!spsc_queue_pop(q, &pkt)) {
            // Spin
        }
        if (pkt.timestamp_ns != i) {
            printf("Error: Expected timestamp %lu, got %lu\n", i, pkt.timestamp_ns);
            assert(pkt.timestamp_ns == i);
        }
        if (pkt.data[0] != (uint8_t)(i & 0xFF)) {
            printf("Error: Expected data %u, got %u\n", (uint8_t)(i & 0xFF), pkt.data[0]);
            assert(pkt.data[0] == (uint8_t)(i & 0xFF));
        }
    }
    return NULL;
}

int main() {
    printf("Testing spsc_queue Concurrency...\n");

    q = spsc_queue_create(QUEUE_SIZE);
    assert(q != NULL);

    pthread_t p, c;
    pthread_create(&p, NULL, producer, NULL);
    pthread_create(&c, NULL, consumer, NULL);

    pthread_join(p, NULL);
    pthread_join(c, NULL);

    spsc_queue_destroy(q);
    printf("spsc_queue test passed!\n");
    return 0;
}
