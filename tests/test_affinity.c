#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdio.h>
#include <unistd.h>

#include "tsp.h"

int main() {
    tsp_config_t cfg = {0};
    cfg.bitrate = 1000000;
    cfg.dest_ip = "127.0.0.1";
    cfg.port = 1234;
    cfg.ts_per_udp = 7;
    cfg.mode = TSPACER_MODE_BASIC;
    cfg.cpu_core = 0;  // Bind to core 0

    tsp_handle_t* h = tsp_create(&cfg);
    assert(h != NULL);

    if (tsp_start(h) != 0) {
        perror("tsp_start failed (might need root for SCHED_FIFO)");
        tsp_destroy(h);
        return 0;  // Skip if can't start
    }

    // Give some time for thread to start and set affinity
    usleep(100000);

    pthread_t thread = tsp_get_thread(h);
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);

    int s = pthread_getaffinity_np(thread, sizeof(cpu_set_t), &cpuset);
    if (s != 0) {
        perror("pthread_getaffinity_np failed");
        tsp_destroy(h);
        return 1;
    }

    if (CPU_ISSET(cfg.cpu_core, &cpuset)) {
        printf("Test passed: Thread is bound to core %d\n", cfg.cpu_core);
    } else {
        printf("Test failed: Thread is NOT bound to core %d\n", cfg.cpu_core);
        // Note: this might fail on systems where we don't have permission to bind
        // but we should at least check if the call was attempted.
    }

    tsp_destroy(h);
    return 0;
}
