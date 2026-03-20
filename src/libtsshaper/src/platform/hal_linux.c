#define _GNU_SOURCE
#include "internal.h"
#include <time.h>
#include <sched.h>
#include <pthread.h>
#include <errno.h>

#if defined(__x86_64__) || defined(__i386__)
#include <immintrin.h>
#define CPU_RELAX() _mm_pause()
#elif defined(__aarch64__)
#define CPU_RELAX() __asm__ __volatile__("yield" ::: "memory")
#else
#define CPU_RELAX() __asm__ __volatile__("" ::: "memory")
#endif

uint64_t hal_get_time_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
    return (uint64_t)ts.tv_sec * NS_PER_SEC + ts.tv_nsec;
}

/**
 * 3-stage precision waiting:
 * 1. nanosleep for coarse wait ( > 200us )
 * 2. sched_yield for medium wait ( > 10us )
 * 3. cpu_relax for fine wait ( busy loop )
 */
void hal_precision_wait(uint64_t target_ns) {
    uint64_t now = hal_get_time_ns();
    if (now >= target_ns) return;

    uint64_t diff = target_ns - now;

    // Stage 1: nanosleep (for diff > 200us)
    // We wake up 50us early to compensate for kernel jitter
    if (diff > 200000) {
        struct timespec ts;
        uint64_t sleep_ns = diff - 50000;
        ts.tv_sec = sleep_ns / NS_PER_SEC;
        ts.tv_nsec = sleep_ns % NS_PER_SEC;
        nanosleep(&ts, NULL);
        now = hal_get_time_ns();
    }

    // Stage 2: sched_yield (for diff > 10us)
    while (hal_get_time_ns() < target_ns - 10000) {
        sched_yield();
    }

    // Stage 3: cpu_relax (busy wait)
    while (hal_get_time_ns() < target_ns) {
        CPU_RELAX();
    }
}

int hal_setup_rt(int cpu_affinity, int priority) {
    // Affinity
    if (cpu_affinity >= 0) {
        cpu_set_t cpuset;
        CPU_ZERO(&cpuset);
        CPU_SET(cpu_affinity, &cpuset);
        if (pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset) != 0) {
            // Log warning or handle error
        }
    }

    // Sched FIFO
    if (priority > 0) {
        struct sched_param param;
        param.sched_priority = priority;
        if (pthread_setschedparam(pthread_self(), SCHED_FIFO, &param) != 0) {
            return -errno;
        }
    }

    return 0;
}
