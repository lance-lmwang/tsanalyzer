#define _GNU_SOURCE
#include <time.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include "hal.h"
#include "internal.h"

/**
 * @brief Helper to add nanoseconds to a timespec structure, handling overflows.
 */
static inline void timespec_add_ns(struct timespec *ts, int64_t ns) {
    ts->tv_nsec += ns;
    while (ts->tv_nsec >= 1000000000L) {
        ts->tv_sec++;
        ts->tv_nsec -= 1000000000L;
    }
}

void* pacer_thread_func(void* arg) {
    tsshaper_t* ctx = (tsshaper_t*)arg;

    // Setup real-time scheduling and CPU affinity via HAL
    hal_setup_rt(ctx->cpu_affinity, ctx->sched_priority);

    struct mmsghdr msgs[ctx->io_batch_size];
    struct iovec iovecs[ctx->io_batch_size];
    uint8_t packet_bufs[ctx->io_batch_size][TS_PACKET_SIZE];

    // Initialize mmsghdr structures for batched I/O
    memset(msgs, 0, sizeof(msgs));
    for (int i = 0; i < ctx->io_batch_size; i++) {
        iovecs[i].iov_base = packet_bufs[i];
        iovecs[i].iov_len = TS_PACKET_SIZE;
        msgs[i].msg_hdr.msg_iov = &iovecs[i];
        msgs[i].msg_hdr.msg_iovlen = 1;
        msgs[i].msg_hdr.msg_name = &ctx->output_addr;
        msgs[i].msg_hdr.msg_namelen = sizeof(ctx->output_addr);
    }

    // Initialize dual-clock state machine for Phase-Locked Loop (PLL)
    struct timespec next_wakeup_ts;
    clock_gettime(CLOCK_MONOTONIC_RAW, &next_wakeup_ts);

    uint64_t expected_send_time_ns = hal_get_time_ns();
    ctx->ideal_packet_time_ns = expected_send_time_ns;

    while (ctx->running) {
        int batch_count = 0;

        for (int i = 0; i < (int)ctx->io_batch_size; i++) {
            // 1. Calculate Scheduling Error (Actual vs Ideal Grid)
            uint64_t actual_time_ns = hal_get_time_ns();
            int64_t error_ns = (int64_t)actual_time_ns - (int64_t)expected_send_time_ns;

            // 2. PI Controller Feedback Loop
            // Convert error to milliseconds for the PI engine to avoid overflow in Q16.16
            float error_ms = (float)error_ns / 1000000.0f;
            int32_t q16_adj_ms = tss_pi_update(&ctx->pacer_pi, FLOAT_TO_Q16(error_ms));
            int64_t adj_ns = (int64_t)(Q16_TO_FLOAT(q16_adj_ms) * 1000000.0f);

            // 3. Compute absolute next wakeup time with PI compensation
            // Formula: next_sleep = base_interval - adjustment
            int64_t current_interval_ns = (int64_t)ctx->packet_interval_ns - adj_ns;
            if (current_interval_ns < 0) current_interval_ns = 0;

            timespec_add_ns(&next_wakeup_ts, current_interval_ns);
            expected_send_time_ns += ctx->packet_interval_ns; // Advance theoretical CBR grid

            // 4. High-Precision Absolute Sleep
            // TIMER_ABSTIME prevents cumulative drift from interrupted sleep or context switches
            clock_nanosleep(CLOCK_MONOTONIC_RAW, TIMER_ABSTIME, &next_wakeup_ts, NULL);

            // 5. Packet Selection & JIT Synthesis
            ts_packet_t* pkt = interleaver_select(ctx);
            if (pkt) {
                memcpy(packet_bufs[i], pkt->data, TS_PACKET_SIZE);
                batch_count++;
            } else {
                // Synthesize NULL packet to maintain strict physical CBR
                memcpy(packet_bufs[i], ctx->null_pkt, TS_PACKET_SIZE);
                batch_count++;
                ctx->null_packets_inserted++;
            }
        }

        // 6. Precision Burst Emission via sendmmsg
        if (batch_count > 0 && ctx->output_fd >= 0) {
            sendmmsg(ctx->output_fd, msgs, batch_count, 0);
            ctx->bytes_sent += batch_count * TS_PACKET_SIZE;
        }
    }

    return NULL;
}

int tsshaper_start_pacer(tsshaper_t* ctx, int fd) {
    if (ctx->running) return -1;
    ctx->output_fd = fd;
    ctx->running = true;
    if (pthread_create(&ctx->pacer_thread, NULL, pacer_thread_func, ctx) != 0) {
        ctx->running = false;
        return -1;
    }
    return 0;
}

void tsshaper_stop_pacer(tsshaper_t* ctx) {
    if (!ctx->running) return;
    ctx->running = false;
    pthread_join(ctx->pacer_thread, NULL);
}
