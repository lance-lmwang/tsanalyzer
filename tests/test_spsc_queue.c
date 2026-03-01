#include <assert.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <sched.h>
#include <unistd.h>

#include "spsc_queue.h"

#define QUEUE_SIZE 1024
#define MSG_COUNT 1000000
#define TIMEOUT_SEC 10

static spsc_queue_t* q;
static _Atomic bool test_failed = false;

static uint64_t get_now_ms() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

void* producer(void* arg) {
    (void)arg;
    uint64_t start_ms = get_now_ms();
    for (uint64_t i = 0; i < MSG_COUNT; i++) {
        ts_packet_t pkt;
        pkt.timestamp_ns = i;
        memset(pkt.data, (uint8_t)(i & 0xFF), 188);
        
        int spins = 0;
        while (!spsc_queue_push(q, &pkt)) {
            if (++spins > 1000) {
                sched_yield();
                spins = 0;
            }
            if (get_now_ms() - start_ms > TIMEOUT_SEC * 1000) {
                printf("Producer Timeout!\n");
                test_failed = true;
                return NULL;
            }
        }
    }
    return NULL;
}

void* consumer(void* arg) {
    (void)arg;
    ts_packet_t pkt;
    uint64_t start_ms = get_now_ms();
    for (uint64_t i = 0; i < MSG_COUNT; i++) {
        int spins = 0;
        while (!spsc_queue_pop(q, &pkt)) {
            if (++spins > 1000) {
                sched_yield();
                spins = 0;
            }
            if (get_now_ms() - start_ms > TIMEOUT_SEC * 1000) {
                printf("Consumer Timeout!\n");
                test_failed = true;
                return NULL;
            }
        }
        if (pkt.timestamp_ns != i) {
            printf("Error: Expected timestamp %lu, got %lu\n", i, pkt.timestamp_ns);
            test_failed = true;
            return NULL;
        }
    }
    return NULL;
}

int main() {
    printf("Testing spsc_queue Concurrency (with 10s internal timeout)...\n");

    q = spsc_queue_create(QUEUE_SIZE);
    assert(q != NULL);

    pthread_t p, c;
    pthread_create(&p, NULL, producer, NULL);
    pthread_create(&c, NULL, consumer, NULL);

    pthread_join(p, NULL);
    pthread_join(c, NULL);

    spsc_queue_destroy(q);
    
    if (test_failed) {
        printf("spsc_queue test FAILED due to timeout or error.\n");
        return 1;
    }

    printf("spsc_queue test passed!\n");
    return 0;
}
