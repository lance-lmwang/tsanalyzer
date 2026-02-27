#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <pthread.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>
#include <sched.h>

#include "tsa.h"
#include "alloc_tracker.h"

#define BATCH_SIZE 1000000
#define NUM_THREADS 4

typedef struct {
    int thread_id;
    uint64_t pkts_processed;
    uint64_t mallocs_detected;
} thread_data_t;

static volatile int g_keep_running = 1;

void* benchmark_worker(void* arg) {
    thread_data_t* data = (thread_data_t*)arg;
    
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(data->thread_id % sysconf(_SC_NPROCESSORS_ONLN), &cpuset);
    pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);

    tsa_config_t cfg = {0};
    tsa_handle_t* h = tsa_create(&cfg);
    if (!h) return NULL;

    uint8_t pkt[188];
    memset(pkt, 0x47, 188);
    pkt[1] = 0x01; // PID 0x100

    // Warmup
    for (int i = 0; i < 1000; i++) tsa_process_packet(h, pkt, i);

    uint64_t m0 = get_malloc_count();
    start_allocation_tracking();

    for (uint64_t i = 0; i < BATCH_SIZE && g_keep_running; i++) {
        tsa_process_packet(h, pkt, i);
        data->pkts_processed++;
    }

    stop_allocation_tracking();
    uint64_t m1 = get_malloc_count();
    data->mallocs_detected = m1 - m0;

    tsa_destroy(h);
    return NULL;
}

int main() {
    mlockall(MCL_CURRENT | MCL_FUTURE);

    pthread_t threads[NUM_THREADS];
    thread_data_t tdata[NUM_THREADS];

    printf("Starting Zero-Malloc Audit (%d threads, %d pkts/thread)...\n", NUM_THREADS, BATCH_SIZE);

    for (int i = 0; i < NUM_THREADS; i++) {
        tdata[i].thread_id = i;
        tdata[i].pkts_processed = 0;
        tdata[i].mallocs_detected = 0;
        pthread_create(&threads[i], NULL, benchmark_worker, &tdata[i]);
    }

    uint64_t total_mallocs = 0;
    for (int i = 0; i < NUM_THREADS; i++) {
        pthread_join(threads[i], NULL);
        total_mallocs += tdata[i].mallocs_detected;
    }

    printf("--------------------------------------------------\n");
    printf("Total Packets   : %lu\n", (uint64_t)NUM_THREADS * BATCH_SIZE);
    printf("Mallocs Detected: %lu\n", total_mallocs);
    printf("--------------------------------------------------\n");

    if (total_mallocs == 0) {
        printf("RESULT: Zero-Malloc Contract PASSED\n");
    } else {
        printf("RESULT: Zero-Malloc Contract FAILED\n");
        return 1;
    }

    return 0;
}
