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
    ctx->packet_interval_ns = (188ULL * 8 * 1000000000ULL) / cfg->bitrate_bps;
    ctx->io_batch_size = cfg->io_batch_size > 0 ? cfg->io_batch_size : 7;
    ctx->use_raw_clock = cfg->use_raw_clock;

    // Initialize NULL packet (0x1FFF PID)
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
    ctx->programs[0].ingest_queue = spsc_queue_create(1024);  // Internal buffer
    ctx->programs[0].wfq_weight = 1.0;

    return ctx;
}

void tsshaper_destroy(tsshaper_t* ctx) {
    if (!ctx) return;
    tsshaper_stop_pacer(ctx);
    // Free programs and queues
    for (int i = 0; i < ctx->num_programs; i++) {
        if (ctx->programs[i].ingest_queue) {
            spsc_queue_free(ctx->programs[i].ingest_queue);
        }
    }
    free(ctx);
}

int tsshaper_push(tsshaper_t* ctx, uint16_t pid, const uint8_t* pkt, tss_time_ns arrival_ts) {
    // tss_debug(ctx, "TSA: Pushing packet pid=%d", pid);
    // Find the correct program for this PID
    // Simplification: use the first program for now
    if (ctx->num_programs == 0) return -1;
    program_ctx_t* prog = &ctx->programs[0];

    // Check backpressure via HWM
    if (tstd_check_backpressure(prog, pid)) {
        tss_warn(ctx, "Backpressure triggered for PID %d", pid);
        return -1;
    }

    // Wrap the packet with metadata
    ts_packet_t meta_pkt;
    memcpy(meta_pkt.data, pkt, TS_PACKET_SIZE);
    meta_pkt.pid = pid;
    meta_pkt.arrival_ns = (arrival_ts > 0) ? arrival_ts : hal_get_time_ns();

    if (spsc_queue_push(prog->ingest_queue, &meta_pkt) != 0) {
        // tss_warn(ctx, "Ingest queue full for PID %d", pid);
        return -1;
    }

    tstd_update_on_push(prog, &meta_pkt);
    return 0;
}

tss_time_ns tsshaper_pull(tsshaper_t* ctx, uint8_t* out_pkt) {
    // tss_debug(ctx, "TSA: Pulling packet");

    // First, try to select a useful packet from the interleaver
    ts_packet_t* pkt = interleaver_select(ctx);

    // If no useful packet is available, or if it's too early for VBR (not implemented yet),
    // we MUST emit a NULL packet to maintain CBR timing.
    // In a pure CBR shaper, we always emit *something* at every interval.

    if (!pkt) {
        // Insert NULL packet
        memcpy(out_pkt, ctx->null_pkt, TS_PACKET_SIZE);
        ctx->null_packets_inserted++;
    } else {
        memcpy(out_pkt, pkt->data, TS_PACKET_SIZE);

        // Update T-STD state on pop
        // Use the first program for now (since interleaver_select already picked it)
        if (ctx->num_programs > 0) {
            // Note: interleaver_select should ideally return the program context too,
            // but for now we assume simple mapping or global update.
            // Actually interleaver_select calls tstd_update_on_pop internally!
            // So we don't need to call it again here.
        }
    }

    // Advance the virtual clock
    if (ctx->next_packet_time_ns == 0) {
        ctx->next_packet_time_ns = hal_get_time_ns();
    }
    ctx->next_packet_time_ns += ctx->packet_interval_ns;

    return ctx->next_packet_time_ns;
}

void tsshaper_get_stats(tsshaper_t* ctx, tsshaper_stats_t* stats) {
    if (!ctx || !stats) return;
    stats->current_bitrate_bps = (double)ctx->total_bitrate_bps;
    stats->buffer_fullness_pct = 0;  // Aggregated from programs
    stats->null_packets_inserted = ctx->null_packets_inserted;
    stats->pcr_jitter_ns = 0;
    stats->continuity_errors = 0;
}
