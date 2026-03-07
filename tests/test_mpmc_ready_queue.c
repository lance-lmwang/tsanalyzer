#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "mpmc_queue.h"

#define NUM_STREAMS 10000
#define NUM_PRODUCERS 4
#define NUM_CONSUMERS 4
#define BURSTS_PER_STREAM 10

typedef struct {
    uint32_t id;
    _Atomic bool scheduled;
    _Atomic uint32_t process_count;
} stream_state_t;

stream_state_t g_streams[NUM_STREAMS];
mpmc_queue_t* g_ready_queue;
_Atomic bool g_running = true;

void* producer_thread(void* arg) {
    int id = *(int*)arg;
    for (int burst = 0; burst < BURSTS_PER_STREAM; burst++) {
        for (int i = 0; i < NUM_STREAMS; i++) {
            // Simulate random subset being active for this producer
            if ((i % NUM_PRODUCERS) == id) {
                // Edge-triggered push logic
                if (!atomic_load_explicit(&g_streams[i].scheduled, memory_order_relaxed)) {
                    if (!atomic_exchange_explicit(&g_streams[i].scheduled, true, memory_order_acq_rel)) {
                        bool success __attribute__((unused)) = mpmc_queue_push(g_ready_queue, i);
                        assert(success);
                    }
                }
            }
        }
        usleep(1000);  // 1ms between bursts
    }
    return NULL;
}

void* consumer_thread(void* arg) {
    (void)arg;
    uint32_t stream_id;
    while (atomic_load(&g_running)) {
        if (mpmc_queue_pop(g_ready_queue, &stream_id)) {
            assert(stream_id < NUM_STREAMS);
            atomic_fetch_add(&g_streams[stream_id].process_count, 1);
            // Simulate work
            // Clear scheduled flag after time slice (or immediately for test)
            atomic_store_explicit(&g_streams[stream_id].scheduled, false, memory_order_release);
        } else {
            usleep(100);
        }
    }
    return NULL;
}

int main() {
    printf("Starting MPMC Edge-Triggered Queue Stress Test...\n");
    g_ready_queue = mpmc_queue_create(16384);  // Bounded power-of-2 queue
    assert(g_ready_queue != NULL);

    for (int i = 0; i < NUM_STREAMS; i++) {
        g_streams[i].id = i;
        atomic_init(&g_streams[i].scheduled, false);
        atomic_init(&g_streams[i].process_count, 0);
    }

    pthread_t producers[NUM_PRODUCERS];
    pthread_t consumers[NUM_CONSUMERS];
    int p_ids[NUM_PRODUCERS];

    for (int i = 0; i < NUM_CONSUMERS; i++) {
        pthread_create(&consumers[i], NULL, consumer_thread, NULL);
    }

    for (int i = 0; i < NUM_PRODUCERS; i++) {
        p_ids[i] = i;
        pthread_create(&producers[i], NULL, producer_thread, &p_ids[i]);
    }

    for (int i = 0; i < NUM_PRODUCERS; i++) {
        pthread_join(producers[i], NULL);
    }

    // Wait for queue to empty
    usleep(50000);
    atomic_store(&g_running, false);

    for (int i = 0; i < NUM_CONSUMERS; i++) {
        pthread_join(consumers[i], NULL);
    }

    // Verify all streams were processed exactly BURSTS_PER_STREAM times
    for (int i = 0; i < NUM_STREAMS; i++) {
        assert(atomic_load(&g_streams[i].process_count) == BURSTS_PER_STREAM);
    }

    mpmc_queue_destroy(g_ready_queue);
    printf("test_mpmc_ready_queue PASS\n");
    return 0;
}
