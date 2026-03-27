#define _GNU_SOURCE
#include <netinet/in.h>
#include <pthread.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include "hal.h"
#include "internal.h"

typedef struct {
    int fd;
    struct sockaddr_in addr;
} linux_backend_t;

static int linux_io_init(tsshaper_t* ctx, void* params) {
    linux_backend_t* backend = calloc(1, sizeof(linux_backend_t));
    if (!backend) return -1;
    if (params) memcpy(backend, params, sizeof(linux_backend_t));
    ctx->backend_priv = backend;
    return 0;
}

static int linux_io_send(tsshaper_t* ctx, struct mmsghdr* msgs, int count) {
    linux_backend_t* backend = (linux_backend_t*)ctx->backend_priv;
    if (!backend || backend->fd < 0) return -1;
    for (int i = 0; i < count; i++) {
        msgs[i].msg_hdr.msg_name = &backend->addr;
        msgs[i].msg_hdr.msg_namelen = sizeof(backend->addr);
    }
    return sendmmsg(backend->fd, msgs, count, 0);
}

static void linux_io_close(tsshaper_t* ctx) {
    if (ctx->backend_priv) {
        free(ctx->backend_priv);
        ctx->backend_priv = NULL;
    }
}

void hal_init_linux_backend(tsshaper_t* ctx) {
    ctx->hal_ops.io_init = linux_io_init;
    ctx->hal_ops.io_send = linux_io_send;
    ctx->hal_ops.io_close = linux_io_close;
}

uint64_t hal_get_linux_time_ns(void) {
    struct timespec ts;
    // Use CLOCK_MONOTONIC_RAW to avoid NTP/PTP slewing
    clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + ts.tv_nsec;
}

void hal_precision_wait_linux(uint64_t target_ns) {
    uint64_t now = hal_get_linux_time_ns();
    while (now < target_ns) {
        uint64_t diff = target_ns - now;
        if (diff > 1000000) {  // > 1ms
            struct timespec ts;
            ts.tv_sec = target_ns / 1000000000ULL;
            ts.tv_nsec = target_ns % 1000000000ULL;
            // TIMER_ABSTIME is crucial: it prevents cumulative drift from interrupted sleep
            clock_nanosleep(CLOCK_MONOTONIC_RAW, TIMER_ABSTIME, &ts, NULL);
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
