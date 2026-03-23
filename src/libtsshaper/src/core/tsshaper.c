#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "hal.h"
#include "internal.h"

// --- Logging Implementation ---

void tsshaper_set_log_callback(tsshaper_t* ctx, tss_log_cb cb, void* opaque) {
    if (ctx) {
        ctx->log_cb = cb;
        ctx->log_opaque = opaque;
    }
}

void tss_log_impl(tsshaper_t* ctx, tss_log_level_t level, const char* fmt, ...) {
    if (ctx && ctx->log_cb) {
        char buffer[1024];
        va_list args;
        va_start(args, fmt);
        vsnprintf(buffer, sizeof(buffer), fmt, args);
        va_end(args);
        ctx->log_cb(level, buffer, ctx->log_opaque);
    }
}

// --- Core Implementation ---

tsshaper_t* tsshaper_create(const tsshaper_config_t* cfg) {
    if (!cfg) return NULL;

    tsshaper_t* ctx = (tsshaper_t*)calloc(1, sizeof(tsshaper_t));
    if (!ctx) return NULL;

    ctx->total_bitrate_bps = cfg->bitrate_bps;
    // Standard MPEG-TS Packet Interval Calculation for CBR (ISO 13818-1)
    ctx->packet_interval_ns = (188ULL * 8 * 1000000000ULL) / cfg->bitrate_bps;
    ctx->io_batch_size = cfg->io_batch_size > 0 ? cfg->io_batch_size : 7;
    ctx->use_raw_clock = cfg->use_raw_clock;

    // Initialize NULL packet (0x1FFF PID) - Broadcast Grade Template
    memset(ctx->null_pkt, 0, TS_PACKET_SIZE);
    ctx->null_pkt[0] = 0x47;
    ctx->null_pkt[1] = 0x1F;
    ctx->null_pkt[2] = 0xFF;
    ctx->null_pkt[3] = 0x10;  // CC=0, Payload only
    memset(ctx->null_pkt + 4, 0xFF, TS_PACKET_SIZE - 4);

    // Default: Initialize one program for all PIDs
    ctx->num_programs = 1;
    ctx->programs[0].active = true;
    ctx->programs[0].program_id = 1;
    ctx->programs[0].target_bitrate_bps = cfg->bitrate_bps;
    for (int p = 0; p < MAX_PRIO; p++) {
        ctx->programs[0].queues[p] = spsc_queue_create(1024);
    }
    ctx->programs[0].wfq_weight = 1.0;
    ctx->programs[0].parent_ctx = ctx;

    // Initialize Pacer PI Controller for clock stability (Weak P, Very Weak I)
    // Limits: +/- 10ms adjustment, 1s integral window.
    tss_pi_init(&ctx->pacer_pi, 0.1f, 0.005f, 10.0f, -10.0f, 1000.0f, -1000.0f);

    return ctx;
}

void tsshaper_destroy(tsshaper_t* ctx) {
    if (!ctx) return;
    tsshaper_stop_pacer(ctx);
    // Free programs and queues
    for (int i = 0; i < ctx->num_programs; i++) {
        for (int p = 0; p < MAX_PRIO; p++) {
            if (ctx->programs[i].queues[p]) {
                spsc_queue_free(ctx->programs[i].queues[p]);
            }
        }
    }
    free(ctx);
}

int tsshaper_push(tsshaper_t* ctx, uint16_t pid, const uint8_t* pkt, tss_time_ns arrival_ts) {
    if (!ctx || ctx->num_programs == 0) return -1;
    program_ctx_t* prog = &ctx->programs[0];

    // TR 101 290 T-STD Buffer Monitoring (Backpressure)
    if (tstd_check_backpressure(prog, pid)) {
        return -1;
    }

    // Wrap the packet with metadata
    ts_packet_t meta_pkt;
    memcpy(meta_pkt.data, pkt, TS_PACKET_SIZE);
    meta_pkt.pid = pid;
    meta_pkt.arrival_ns = (arrival_ts > 0) ? arrival_ts : hal_get_time_ns();

    // Call T-STD update first to determine priority (PAT/PMT/PCR)
    tstd_update_on_push(prog, &meta_pkt);

    // Identify priority for the interleaver
    packet_prio_t prio = PRIO_MEDIUM;
    for (int i = 0; i < prog->num_pids; i++) {
        if (prog->pids[i].pid == pid) {
            prio = prog->pids[i].priority;
            break;
        }
    }

    if (!prog->queues[prio]) {
        fprintf(stderr, "ERROR: Queue for priority %d not initialized!\n", prio);
        return -1;
    }

    if (!spsc_queue_push(prog->queues[prio], &meta_pkt)) {
        return -1;
    }

    return 0;
}

tss_time_ns tsshaper_pull(tsshaper_t* ctx, uint8_t* out_pkt) {
    uint64_t now_ns = hal_get_time_ns();

    // Select the best packet based on ISO 13818-1 / TR 101 290 priorities
    ts_packet_t* pkt = interleaver_select(ctx);

    if (!pkt) {
        // Strict CBR: Always insert NULL packet if no data available
        memcpy(out_pkt, ctx->null_pkt, TS_PACKET_SIZE);
        atomic_fetch_add(&ctx->null_packets_inserted, 1);
    } else {
        memcpy(out_pkt, pkt->data, TS_PACKET_SIZE);
        // Note: tstd_update_on_pop was already called inside interleaver_select!
    }

    // Advance the virtual clock to the exact point of emission
    if (ctx->next_packet_time_ns == 0) {
        ctx->next_packet_time_ns = now_ns;
        ctx->start_time_ns = now_ns;
    }
    ctx->next_packet_time_ns += ctx->packet_interval_ns;

    return ctx->next_packet_time_ns;
}

void tsshaper_get_stats(tsshaper_t* ctx, tsshaper_stats_t* stats) {
    if (!ctx || !stats) return;
    stats->current_bitrate_bps = (double)ctx->total_bitrate_bps;
    stats->null_packets_inserted = atomic_load(&ctx->null_packets_inserted);
    stats->buffer_fullness_pct = 0;
    stats->pcr_jitter_ns = 0;
    stats->continuity_errors = 0;
}
