#define _GNU_SOURCE
#include <pthread.h>
#include <sched.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>
#include "hal.h"
#include "internal.h"

uint64_t hal_get_time_ns(void) {
    struct timespec ts;
    // Use CLOCK_MONOTONIC_RAW to avoid NTP/PTP slewing
    clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + ts.tv_nsec;
}

void hal_precision_wait(uint64_t target_ns) {
    uint64_t now = hal_get_time_ns();
    while (now < target_ns) {
        uint64_t diff = target_ns - now;
        if (diff > 1000000) {  // > 1ms
            struct timespec ts;
            ts.tv_sec = 0;
            ts.tv_nsec = 500000;  // 0.5ms sleep
            nanosleep(&ts, NULL);
        } else if (diff > 10000) {  // > 10us
            sched_yield();
        } else {
            // Busy wait with PAUSE instruction
            __builtin_ia32_pause();
        }
        now = hal_get_time_ns();
    }
}

int hal_setup_rt(int cpu_affinity, int priority) {
    if (cpu_affinity >= 0) {
        cpu_set_t cpuset;
        CPU_ZERO(&cpuset);
        CPU_SET(cpu_affinity, &cpuset);
        if (pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset) != 0) {
            // Error handling left to caller via errno
        }
    }

    if (priority > 0) {
        struct sched_param param;
        param.sched_priority = priority;
        if (pthread_setschedparam(pthread_self(), SCHED_FIFO, &param) != 0) {
            return -1;
        }
    }
    return 0;
}
