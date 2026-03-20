#include "internal.h"
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>

extern void* pacer_thread_func(void* arg);

tsa_shaper_t* tsa_shaper_create(uint64_t total_bitrate) {
    tsa_shaper_t* ctx = calloc(1, sizeof(tsa_shaper_t));
    if (!ctx) return NULL;

    ctx->total_bitrate_bps = total_bitrate;
    if (total_bitrate > 0) {
        ctx->packet_interval_ns = (uint64_t)TS_PACKET_SIZE * 8 * NS_PER_SEC / total_bitrate;
    } else {
        ctx->packet_interval_ns = 1000000; // Default
    }

    ctx->output_fd = -1;
    ctx->running = true;

    // Default RT settings
    ctx->cpu_affinity = -1;
    ctx->sched_priority = 0;

    if (pthread_create(&ctx->pacer_thread, NULL, pacer_thread_func, ctx) != 0) {
        free(ctx);
        return NULL;
    }

    return ctx;
}

void tsa_shaper_destroy(tsa_shaper_t* ctx) {
    if (!ctx) return;

    ctx->running = false;
    pthread_join(ctx->pacer_thread, NULL);

    for (int i = 0; i < ctx->num_programs; i++) {
        if (ctx->programs[i].ingest_queue) {
            spsc_queue_destroy(ctx->programs[i].ingest_queue);
        }
    }

    if (ctx->output_fd >= 0) {
        close(ctx->output_fd);
    }

    free(ctx);
}

int tsa_shaper_add_program(tsa_shaper_t* ctx, int program_id) {
    if (ctx->num_programs >= MAX_PROGRAMS) return -1;

    program_ctx_t* prog = &ctx->programs[ctx->num_programs++];
    prog->program_id = program_id;
    prog->active = true;
    prog->ingest_queue = spsc_queue_create(1024);

    // Initial equal distribution
    for (int i = 0; i < ctx->num_programs; i++) {
        ctx->programs[i].wfq_weight = 1.0 / ctx->num_programs;
        ctx->programs[i].current_bitrate_bps = ctx->total_bitrate_bps / ctx->num_programs;
    }

    return 0;
}

int tsa_shaper_set_program_bitrate(tsa_shaper_t* ctx, int program_id, uint64_t bps) {
    for (int i = 0; i < ctx->num_programs; i++) {
        if (ctx->programs[i].program_id == program_id) {
            ctx->programs[i].target_bitrate_bps = bps;
            return 0;
        }
    }
    return -1;
}

int tsa_shaper_push(tsa_shaper_t* ctx, int program_id, const uint8_t* ts_packet) {
    for (int i = 0; i < ctx->num_programs; i++) {
        program_ctx_t* prog = &ctx->programs[i];
        if (prog->program_id == program_id) {
            ts_packet_t pkt;
            memcpy(pkt.data, ts_packet, TS_PACKET_SIZE);
            pkt.timestamp_ns = hal_get_time_ns();

            uint16_t pid = ((ts_packet[1] & 0x1F) << 8) | ts_packet[2];
            if (tstd_check_backpressure(prog, pid)) {
                return -2; // Backpressure (95% HWM)
            }

            if (spsc_queue_push(prog->ingest_queue, &pkt)) {
                tstd_update_on_push(prog, &pkt);
                return 0;
            } else {
                return -1; // Queue full
            }
        }
    }
    return -3; // Program not found
}

int tsa_shaper_set_output(tsa_shaper_t* ctx, tsa_output_mode_t mode, const char* url) {
    ctx->output_mode = mode;
    if (url) {
        strncpy(ctx->output_url, url, sizeof(ctx->output_url) - 1);
    }

    if (mode == TSA_OUT_UDP || mode == TSA_OUT_RTP) {
        char host[256] = {0};
        int port = 0;
        if (url && sscanf(url, "udp://%[^:]:%d", host, &port) == 2) {
            ctx->output_fd = socket(AF_INET, SOCK_DGRAM, 0);
            if (ctx->output_fd < 0) return -1;

            memset(&ctx->output_addr, 0, sizeof(ctx->output_addr));
            ctx->output_addr.sin_family = AF_INET;
            ctx->output_addr.sin_port = htons(port);
            inet_pton(AF_INET, host, &ctx->output_addr.sin_addr);

            int flags = fcntl(ctx->output_fd, F_GETFL, 0);
            fcntl(ctx->output_fd, F_SETFL, flags | O_NONBLOCK);
        }
    }

    return 0;
}

void tsa_shaper_get_stats(tsa_shaper_t* ctx, tsa_shaper_stats_t* stats) {
    stats->bytes_sent = atomic_load(&ctx->bytes_sent);
    uint64_t count = atomic_load(&ctx->pcr_count);
    if (count > 0) {
        stats->pcr_jitter_ns = (double)atomic_load((_Atomic uint64_t*)&ctx->pcr_jitter_ns_sum) / count;
    } else {
        stats->pcr_jitter_ns = 0;
    }

    double total_fullness = 0;
    int pid_count = 0;
    for (int i = 0; i < ctx->num_programs; i++) {
        for (int j = 0; j < ctx->programs[i].num_pids; j++) {
            uint32_t fullness = atomic_load(&ctx->programs[i].pids[j].buffer_fullness);
            total_fullness += (double)fullness / ctx->programs[i].pids[j].buffer_size;
            pid_count++;
        }
    }
    stats->buffer_fullness_avg = pid_count > 0 ? total_fullness / pid_count : 0;
}
