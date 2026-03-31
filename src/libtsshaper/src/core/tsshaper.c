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
    ctx->is_offline = cfg->is_offline;
    ctx->strict_cbr = cfg->strict_cbr;

    // Initialize NULL packet (0x1FFF PID) - Broadcast Grade Template
    memset(ctx->null_pkt, 0, TS_PACKET_SIZE);
    ctx->null_pkt[0] = 0x47;
    ctx->null_pkt[1] = 0x1F;
    ctx->null_pkt[2] = 0xFF;
    ctx->null_pkt[3] = 0x10;  // CC=0, Payload only
    memset(ctx->null_pkt + 4, 0xFF, TS_PACKET_SIZE - 4);

    // PCR Interpolation Engine Initialization
    ctx->pcr_interval_ms = cfg->pcr_interval_ms > 0 ? cfg->pcr_interval_ms : 30;  // Broadcast 30ms default
    ctx->master_pcr_pid = 0x1FFF;                                                 // 0x1FFF signifies none detected yet
    ctx->last_pcr_time_ns = 0;

    // Default: Initialize one program for all PIDs
    ctx->num_programs = 1;
    ctx->programs[0].active = true;
    ctx->programs[0].program_id = 1;
    ctx->programs[0].target_bitrate_bps = cfg->bitrate_bps;
    ctx->programs[0].wfq_weight = 1.0;
    ctx->programs[0].parent_ctx = ctx;

    // Initialize Program PI for statmux weight rebalancing
    tss_pi_init(&ctx->programs[0].pi, 0.1f, 0.01f, 0.2f, -0.2f, 1.0f, -1.0f);

    // Initialize Pacer PI Controller for clock stability (Weak P, Very Weak I)
    // Limits: +/- 10ms adjustment, 1s integral window.
    tss_pi_init(&ctx->pacer_pi, 0.1f, 0.005f, 10.0f, -10.0f, 1000.0f, -1000.0f);

    // Initialize HAL Ops and then the specific I/O backend
    if (cfg->backend == 2) {  // TSS_BACKEND_CALLBACK
        void hal_init_callback_backend(tsshaper_t * ctx, tss_write_cb cb, void* opaque);
        hal_init_callback_backend(ctx, cfg->write_cb, cfg->write_opaque);
        tss_info(ctx, "TS Shaper initialized in CALLBACK mode (AVIO compatible)");
    } else {
        hal_init_ops(ctx, cfg->backend);
        if (ctx->hal_ops.io_init) {
            ctx->hal_ops.io_init(ctx, cfg->backend_params);
        }
        tss_info(ctx, "TS Shaper initialized in NETWORK/FILE mode (Direct I/O)");
    }

    return ctx;
}

void tsshaper_destroy(tsshaper_t* ctx) {
    if (!ctx) return;
    tsshaper_stop_pacer(ctx);

    // Professional cleanup of abstracted I/O via ops
    if (ctx->hal_ops.io_close) {
        ctx->hal_ops.io_close(ctx);
    }

    // Free programs and per-pid queues
    for (int i = 0; i < ctx->num_programs; i++) {
        for (int p = 0; p < ctx->programs[i].num_pids; p++) {
            if (ctx->programs[i].pids[p].queue) {
                spsc_queue_free(ctx->programs[i].pids[p].queue);
            }
        }
    }
    free(ctx);
}

int tsshaper_push(tsshaper_t* ctx, uint16_t pid, const uint8_t* pkt, tss_time_ns arrival_ts) {
    if (!ctx || ctx->num_programs == 0) return -1;
    program_ctx_t* prog = &ctx->programs[0];

    if (tstd_check_backpressure(prog, pid)) {
        return -1;
    }

    ts_packet_t meta_pkt;
    memcpy(meta_pkt.data, pkt, TS_PACKET_SIZE);
    meta_pkt.pid = pid;

    if (ctx->running) {
        meta_pkt.arrival_ns = (arrival_ts > (tss_time_ns)0) ? (uint64_t)arrival_ts : hal_get_time_ns();
    } else {
        meta_pkt.arrival_ns = (arrival_ts > (tss_time_ns)0) ? (uint64_t)arrival_ts : ctx->next_packet_time_ns;
    }

    tstd_update_on_push(prog, &meta_pkt);

    tstd_pid_ctx_t* pid_ctx = NULL;
    for (int i = 0; i < prog->num_pids; i++) {
        if (prog->pids[i].pid == pid) {
            pid_ctx = &prog->pids[i];
            break;
        }
    }

    if (!pid_ctx || !pid_ctx->queue) {
        return -1;
    }

    if (!spsc_queue_push(pid_ctx->queue, &meta_pkt)) {
        return -1;
    }

    return 0;
}

tss_time_ns tsshaper_pull(tsshaper_t* ctx, uint8_t* out_pkt) {
    uint64_t now_ns;

    if (ctx->running) {
        now_ns = hal_get_time_ns();
        if (ctx->next_packet_time_ns == 0) {
            ctx->next_packet_time_ns = now_ns;
            ctx->start_time_ns = now_ns;
        }
    } else {
        if (ctx->next_packet_time_ns == 0) {
            ctx->next_packet_time_ns = 1000000000ULL;  // 1s epoch for offline
            ctx->start_time_ns = ctx->next_packet_time_ns;
        }
        now_ns = ctx->next_packet_time_ns;
    }

    ctx->ideal_packet_time_ns = ctx->next_packet_time_ns;

    if (ctx->master_pcr_pid != 0x1FFF && ctx->start_time_ns > 0) {
        uint64_t elapsed_since_last_pcr_ns = ctx->ideal_packet_time_ns - ctx->last_pcr_time_ns;
        if (ctx->last_pcr_time_ns == 0 || elapsed_since_last_pcr_ns > (ctx->pcr_interval_ms * 1000000ULL)) {
            // Synthesize a PCR-only packet
            memset(out_pkt, 0xFF, TS_PACKET_SIZE);
            out_pkt[0] = 0x47;
            out_pkt[1] = (ctx->master_pcr_pid >> 8) & 0x1F;
            out_pkt[2] = ctx->master_pcr_pid & 0xFF;
            out_pkt[3] = 0x20;  // Adaptation Field only, no payload, CC=0 (doesn't increment)
            out_pkt[4] = 183;   // AF Length
            out_pkt[5] = 0x10;  // PCR Flag set

            // Calculate PCR value using 27MHz clock reconstruction
            uint64_t elapsed_ns = ctx->ideal_packet_time_ns - ctx->start_time_ns;
            uint64_t ticks_27m = (elapsed_ns * 27) / 1000;
            uint64_t current_pcr_42 = ctx->start_pcr_base + ticks_27m;

            uint64_t base = current_pcr_42 / 300;
            uint32_t ext = current_pcr_42 % 300;

            out_pkt[6] = (uint8_t)(base >> 25);
            out_pkt[7] = (uint8_t)(base >> 17);
            out_pkt[8] = (uint8_t)(base >> 9);
            out_pkt[9] = (uint8_t)(base >> 1);
            out_pkt[10] = (uint8_t)(((base & 0x01) << 7) | 0x7E | ((ext >> 8) & 0x01));
            out_pkt[11] = (uint8_t)(ext & 0xFF);

            ctx->last_pcr_time_ns = ctx->ideal_packet_time_ns;

            // Advance to next theoretical slot
            ctx->next_packet_time_ns += ctx->packet_interval_ns;
            return ctx->next_packet_time_ns;
        }
    }

    // 2. Select the best packet based on ISO 13818-1 / TR 101 290 priorities
    // and strict PID-level pacing target times.
    ts_packet_t* pkt = interleaver_select(ctx);

    if (!pkt) {
        // Strict CBR: Always insert NULL packet if no data available OR timing not met
        memcpy(out_pkt, ctx->null_pkt, TS_PACKET_SIZE);
        atomic_fetch_add(&ctx->null_packets_inserted, 1);
    } else {
        memcpy(out_pkt, pkt->data, TS_PACKET_SIZE);
        // Track PCR emission from the stream itself to reset the interpolator timer
        if ((out_pkt[3] & 0x20) && out_pkt[4] > 0 && (out_pkt[5] & 0x10)) {
            uint16_t pid = ((out_pkt[1] & 0x1F) << 8) | out_pkt[2];
            if (pid == ctx->master_pcr_pid) {
                ctx->last_pcr_time_ns = ctx->ideal_packet_time_ns;
            }
        }
    }

    // Advance to next theoretical slot
    ctx->next_packet_time_ns += ctx->packet_interval_ns;

    return ctx->next_packet_time_ns;
}

bool tsshaper_is_empty(tsshaper_t* ctx) {
    if (!ctx) return true;
    for (int i = 0; i < ctx->num_programs; i++) {
        program_ctx_t* prog = &ctx->programs[i];
        for (int p = 0; p < prog->num_pids; p++) {
            if (prog->pids[p].queue && spsc_queue_count(prog->pids[p].queue) > 0) {
                return false;
            }
        }
    }
    return true;
}

void tsshaper_get_stats(tsshaper_t* ctx, tsshaper_stats_t* stats) {
    if (!ctx || !stats) return;
    stats->current_bitrate_bps = (double)ctx->total_bitrate_bps;
    stats->null_packets_inserted = atomic_load(&ctx->null_packets_inserted);
    stats->buffer_fullness_pct = 0;
    stats->pcr_jitter_ns = 0;
    stats->continuity_errors = 0;
}

int tsshaper_set_pid_bitrate(tsshaper_t* ctx, uint16_t pid, uint64_t bitrate_bps) {
    if (!ctx || ctx->num_programs == 0) return -1;
    program_ctx_t* prog = &ctx->programs[0];

    tstd_pid_ctx_t* pid_ctx = tstd_find_or_create_pid_ctx(prog, pid);
    if (!pid_ctx) return -1;

    pid_ctx->shaping_rate_bps = bitrate_bps;

    // Recalculate burst credit limit based on new rate (50ms window)
    double max_credit = (double)bitrate_bps * 0.05;
    if (pid_ctx->shaping_credit_bits > max_credit) {
        pid_ctx->shaping_credit_bits = max_credit;
    }

    // Explicitly update priority based on PID type heuristics if needed,
    // or just assume caller knows what they are doing.
    // For now, we trust the existing priority logic unless overridden.

    return 0;
}
