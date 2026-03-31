#define _GNU_SOURCE
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include "hal.h"
#include "internal.h"

/**
 * @brief Helper to add nanoseconds to a timespec structure, handling overflows.
 */
static inline void timespec_add_ns(struct timespec* ts, int64_t ns) {
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
    for (uint32_t i = 0; i < ctx->io_batch_size; i++) {
        iovecs[i].iov_base = packet_bufs[i];
        iovecs[i].iov_len = TS_PACKET_SIZE;
        msgs[i].msg_hdr.msg_iov = &iovecs[i];
        msgs[i].msg_hdr.msg_iovlen = 1;
        // Address management is now handled internally by HAL io_send
    }

    // Initialize dual-clock state machine for Phase-Locked Loop (PLL)
    struct timespec next_wakeup_ts;
    clock_gettime(CLOCK_MONOTONIC_RAW, &next_wakeup_ts);

    uint64_t expected_send_time_ns = 0;

    while (ctx->running) {
        int batch_count = 0;

        // In offline mode, wait for the first real packet to define the start time
        if (ctx->is_offline && ctx->start_time_ns == 0) {
            usleep(100);
            continue;
        }

        // FLOW CONTROL (Offline): Don't run too far ahead of the producer
        if (ctx->is_offline) {
            if (expected_send_time_ns > ctx->last_arrival_ns + 500000000ULL) {
                usleep(1000);  // Wait for producer to catch up
                continue;
            }
        }

        if (expected_send_time_ns == 0) {
            expected_send_time_ns = hal_get_time_ns();
            ctx->ideal_packet_time_ns = expected_send_time_ns;
            clock_gettime(CLOCK_MONOTONIC_RAW, &next_wakeup_ts);
        }

        for (uint32_t i = 0; i < ctx->io_batch_size; i++) {
            uint64_t emission_ts_ns;

            if (ctx->is_offline) {
                // In offline mode, we ignore real-time jitter and PI controllers.
                // We just follow the ideal CBR grid as fast as the CPU allows.
                emission_ts_ns = expected_send_time_ns;
            } else {
                // 1. Calculate Scheduling Error (Actual vs Ideal Grid)
                uint64_t now_ns = hal_get_time_ns();
                int64_t error_ns = (int64_t)now_ns - (int64_t)expected_send_time_ns;

                // SAFETY: If we are behind by more than 100ms, reset the grid to now.
                if (error_ns > 100000000LL) {
                    expected_send_time_ns = now_ns;
                    clock_gettime(CLOCK_MONOTONIC_RAW, &next_wakeup_ts);
                    error_ns = 0;
                }

                // 2. PI Controller Feedback Loop
                float error_ms = (float)error_ns / 1000000.0f;
                int32_t q16_adj_ms = tss_pi_update(&ctx->pacer_pi, FLOAT_TO_Q16(error_ms));
                int64_t adj_ns = (int64_t)(Q16_TO_FLOAT(q16_adj_ms) * 1000000.0f);

                // 3. Compute absolute next wakeup time with PI compensation
                int64_t current_interval_ns = (int64_t)ctx->packet_interval_ns - adj_ns;
                if (current_interval_ns < 0) current_interval_ns = 0;

                timespec_add_ns(&next_wakeup_ts, current_interval_ns);

                // 4. High-Precision Absolute Sleep
                hal_precision_wait(0);
                struct timespec ts_sleep = next_wakeup_ts;
                clock_nanosleep(CLOCK_MONOTONIC_RAW, TIMER_ABSTIME, &ts_sleep, NULL);
                emission_ts_ns = hal_get_time_ns();
            }

            // 5. Update actual emission tracking
            expected_send_time_ns += ctx->packet_interval_ns;

            // 6. Packet Selection & JIT Synthesis
            ts_packet_t* pkt = interleaver_select(ctx);
            if (pkt) {
                memcpy(packet_bufs[i], pkt->data, TS_PACKET_SIZE);

                // JIT PCR Rewrite based on DEFINITIVE emission time
                if ((packet_bufs[i][3] & 0x20) && packet_bufs[i][4] > 0 && (packet_bufs[i][5] & 0x10)) {
                    uint64_t elapsed_ns = emission_ts_ns - ctx->start_time_ns;
                    uint64_t pcr_27m = ctx->start_pcr_base + (elapsed_ns * 27) / 1000;
                    uint64_t base = pcr_27m / 300;
                    uint32_t ext = pcr_27m % 300;

                    uint8_t* pcr_buf = packet_bufs[i] + 6;
                    pcr_buf[0] = (base >> 25) & 0xFF;
                    pcr_buf[1] = (base >> 17) & 0xFF;
                    pcr_buf[2] = (base >> 9) & 0xFF;
                    pcr_buf[3] = (base >> 1) & 0xFF;
                    pcr_buf[4] = ((base & 0x01) << 7) | 0x7E | ((ext >> 8) & 0x01);
                    pcr_buf[5] = ext & 0xFF;
                }

                batch_count++;
            } else {
                memcpy(packet_bufs[i], ctx->null_pkt, TS_PACKET_SIZE);
                // In offline mode, only increment batch_count if we are actually making progress
                // relative to the producer. For now, we still output NULLs to maintain CBR.
                batch_count++;
                ctx->null_packets_inserted++;
            }
        }

        // 7. Precision Burst Emission via abstracted HAL ops
        if (batch_count > 0 && ctx->hal_ops.io_send) {
            ctx->hal_ops.io_send(ctx, msgs, batch_count);
            ctx->bytes_sent += (uint64_t)batch_count * TS_PACKET_SIZE;
        }

        // DEBUG: Log progress periodically in offline mode
        static uint64_t last_log_bytes = 0;
        if (ctx->is_offline && (ctx->bytes_sent - last_log_bytes > 1024 * 1024)) {
            tss_info(ctx, "Pacer Progress: Sent %lu bytes, Virtual Time: %lu ns", (uint64_t)ctx->bytes_sent,
                     expected_send_time_ns);
            last_log_bytes = ctx->bytes_sent;
        }

        // 8. Throttling for Offline Mode to prevent CPU starvation
        if (ctx->is_offline && batch_count > 0) {
            // If the pacer is running too fast in offline mode, it might far exceed
            // the producer's data. We check if the total duration processed
            // is much larger than the real time.
            // Simplified: If we just sent a batch of mostly NULLs, sleep a bit.
            if (batch_count == (int)ctx->io_batch_size && ctx->null_packets_inserted > 1000) {
                // Optimization: Only sleep if we are "idle" churning NULLs
                static int null_streak = 0;
                null_streak++;
                if (null_streak > 100) {
                    usleep(10);
                    null_streak = 0;
                }
            }
        }
    }

    return NULL;
}

int tsshaper_start_pacer(tsshaper_t* ctx, int fd) {
    (void)fd;
    if (ctx->running) return -1;
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
