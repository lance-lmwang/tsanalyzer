#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "tsa_internal.h"
#include "tsa_plugin.h"

typedef struct {
    tsa_handle_t* h;
    tsa_stream_t stream;
} pcr_ctx_t;

static void pcr_on_ts(void* self, const uint8_t* pkt);

static void* pcr_create(void* h) {
    pcr_ctx_t* ctx = calloc(1, sizeof(pcr_ctx_t));
    ctx->h = (tsa_handle_t*)h;
    tsa_stream_init(&ctx->stream, ctx, pcr_on_ts);
    return ctx;
}

static void pcr_destroy(void* engine) {
    pcr_ctx_t* ctx = (pcr_ctx_t*)engine;
    tsa_stream_destroy(&ctx->stream);
    free(ctx);
}

static tsa_stream_t* pcr_get_stream(void* engine) {
    pcr_ctx_t* ctx = (pcr_ctx_t*)engine;
    return &ctx->stream;
}

static void pcr_on_ts(void* self, const uint8_t* pkt) {
    pcr_ctx_t* ctx = (pcr_ctx_t*)self;
    tsa_handle_t* h = ctx->h;
    const ts_decode_result_t* res = &h->current_res;
    uint64_t now = h->current_ns;
    uint16_t pid = res->pid;

    if ((pkt[3] & 0x20) && pkt[4] > 0 && (pkt[5] & 0x10)) {
        h->live->pid_is_referenced[pid] = true;
        tsa_update_pid_tracker(h, pid);

        // Update the ClockInspector
        tsa_clock_update(pkt, &h->clock_inspectors[pid], now);
        
        // Sync metrics to snapshot
        h->live->pcr_jitter_max_ns = (uint64_t)(fabs(h->clock_inspectors[pid].pcr_jitter_ms) * 1000000.0);
        h->live->pcr_repetition_max_ms = h->clock_inspectors[pid].pcr_interval_max_ticks / (PCR_TICKS_PER_MS);
        h->live->pcr_repetition_error.count = h->clock_inspectors[pid].priority_1_errors;

        uint64_t pt = extract_pcr(pkt);

        if (h->last_pcr_ticks > 0 && h->last_pcr_interval_bitrate_bps > 0) {
            uint64_t bits = h->pkts_since_pcr * 1504;
            uint64_t expected = h->last_pcr_ticks + (bits * 27000000 / h->last_pcr_interval_bitrate_bps);
            int64_t diff = (int64_t)pt - (int64_t)expected;
            if (diff < -((int64_t)1 << 41))
                diff += ((int64_t)1 << 42);
            else if (diff > ((int64_t)1 << 41))
                diff -= ((int64_t)1 << 42);
            h->live->pcr_accuracy_ns_piecewise = (double)diff * 1000.0 / 27.0;
        }

        if (h->config.op_mode == TSA_MODE_REPLAY || !h->stc_locked) {
            bool first_lock = !h->stc_locked;
            if (first_lock) {
                h->stc_ns = pt * 1000 / 27;
                h->stc_locked = true;
                // printf("CORE: STC Initial Lock at %lu ns\n", h->stc_ns);
            }
            ts_pcr_window_add(&h->pcr_window, now, (int128_t)pt * 1000 / 27, h->pkts_since_pcr);
            ts_pcr_window_add(&h->pcr_long_window, now, (int128_t)pt * 1000 / 27, h->pkts_since_pcr);
        }

        if (h->last_pcr_ticks > 0) {
            uint64_t dt_ticks = (pt - h->last_pcr_ticks);
            if (dt_ticks < ((uint64_t)1 << 41)) {
                uint64_t interval_br = (h->pkts_since_pcr * 1504 * 27000000ULL) / dt_ticks;
                if (h->live->pcr_bitrate_bps == 0)
                    h->live->pcr_bitrate_bps = interval_br;
                else
                    h->live->pcr_bitrate_bps = (interval_br * 2 + h->live->pcr_bitrate_bps * 8) / 10;
                h->last_pcr_interval_bitrate_bps = interval_br;
            }
        }
        h->last_pcr_ticks = pt;
        h->pkts_since_pcr = 0;
    }
}

static tsa_plugin_ops_t pcr_ops = {
    .name = "PCR_ANALYSIS",
    .create = pcr_create,
    .destroy = pcr_destroy,
    .get_stream = pcr_get_stream,
};

void tsa_register_pcr_engine(tsa_handle_t* h) {
    extern void tsa_plugin_attach_instance(tsa_handle_t* h, tsa_plugin_ops_t* ops);
    tsa_plugin_attach_instance(h, &pcr_ops);
}