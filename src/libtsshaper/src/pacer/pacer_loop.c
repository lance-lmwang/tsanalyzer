#define _GNU_SOURCE
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include "hal.h"
#include "internal.h"

void* pacer_thread_func(void* arg) {
    tsshaper_t* ctx = (tsshaper_t*)arg;

    // Setup real-time scheduling and CPU affinity
    hal_setup_rt(ctx->cpu_affinity, ctx->sched_priority);

    struct mmsghdr msgs[ctx->io_batch_size];
    struct iovec iovecs[ctx->io_batch_size];
    uint8_t packet_bufs[ctx->io_batch_size][TS_PACKET_SIZE];

    memset(msgs, 0, sizeof(msgs));
    for (int i = 0; i < ctx->io_batch_size; i++) {
        iovecs[i].iov_base = packet_bufs[i];
        iovecs[i].iov_len = TS_PACKET_SIZE;
        msgs[i].msg_hdr.msg_iov = &iovecs[i];
        msgs[i].msg_hdr.msg_iovlen = 1;
        msgs[i].msg_hdr.msg_name = &ctx->output_addr;
        msgs[i].msg_hdr.msg_namelen = sizeof(ctx->output_addr);
    }

    ctx->next_packet_time_ns = hal_get_time_ns();

    while (ctx->running) {
        int batch_count = 0;

        for (int i = 0; i < ctx->io_batch_size; i++) {
            // Wait for next scheduled packet time
            hal_precision_wait(ctx->next_packet_time_ns);

            // Get next packet from interleaver (StatMux logic is inside select)
            ts_packet_t* pkt = interleaver_select(ctx);
            if (pkt) {
                // JIT PCR Rewrite based on actual emission time (now)
                // In a perfect system, next_packet_time_ns IS the emission time
                memcpy(packet_bufs[i], pkt->data, TS_PACKET_SIZE);
                // TODO: Rewrite PCR if necessary here
                batch_count++;
            } else {
                // Generate NULL packet to maintain CBR
                memcpy(packet_bufs[i], ctx->null_pkt, TS_PACKET_SIZE);
                batch_count++;
                ctx->null_packets_inserted++;
            }

            // Advance virtual clock for next packet in CBR sequence
            ctx->next_packet_time_ns += ctx->packet_interval_ns;
        }

        // Physical I/O (Batch)
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
