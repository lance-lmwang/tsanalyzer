#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tsa_internal.h"
#include "tsa_plugin.h"

typedef struct {
    tsa_handle_t* h;
    tsa_stream_t stream;
    uint32_t pcr_count_since_regress;
} pcr_ctx_t;

static void pcr_on_ts(void* self, const uint8_t* pkt);

static void* pcr_create(void* h, void* context_buf) {
    pcr_ctx_t* ctx = (pcr_ctx_t*)context_buf;
    memset(ctx, 0, sizeof(pcr_ctx_t));
    ctx->h = (tsa_handle_t*)h;
    tsa_stream_init(&ctx->stream, ctx, pcr_on_ts);
    return ctx;
}

static void pcr_destroy(void* engine) {
    pcr_ctx_t* ctx = (pcr_ctx_t*)engine;
    tsa_stream_destroy(&ctx->stream);
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

        // Update the ClockInspector (handles jitter calculation)
        tsa_clock_update(pkt, &h->clock_inspectors[pid], now);

        h->live->pcr_jitter_max_ns = (uint64_t)(fabs(h->clock_inspectors[pid].pcr_jitter_ms) * 1000000.0);
        h->live->pcr_repetition_max_ms = h->clock_inspectors[pid].pcr_interval_max_ticks / (PCR_TICKS_PER_MS);
        h->live->pcr_repetition_error.count = h->clock_inspectors[pid].priority_1_errors;

        uint64_t pt = extract_pcr(pkt);
        uint64_t pcr_ns = pt * 1000 / 27;

        // Piecewise bitrate analysis
        if (h->last_pcr_ticks > 0 && h->last_pcr_interval_bitrate_bps > 0) {
            uint64_t bits = h->pkts_since_pcr * TS_PACKET_BITS;
            uint64_t expected = h->last_pcr_ticks + (bits * TS_SYSTEM_CLOCK_HZ / h->last_pcr_interval_bitrate_bps);
            int64_t diff = (int64_t)pt - (int64_t)expected;
            if (diff < -((int64_t)1 << 41))
                diff += ((int64_t)1 << 42);
            else if (diff > ((int64_t)1 << 41))
                diff -= ((int64_t)1 << 42);
            h->live->pcr_accuracy_ns_piecewise = (double)diff * 1000.0 / 27.0;
        }

        // Real-time Regression Trigger
        ts_pcr_window_add(&h->pcr_window, now, pcr_ns, h->pkts_since_pcr);
        ts_pcr_window_add(&h->pcr_long_window, now, pcr_ns, h->pkts_since_pcr);

        if (!h->stc_locked) {
            h->stc_ns = pcr_ns;
            h->stc_locked = true;
        }

        if (++ctx->pcr_count_since_regress >= 16) {
            double sl = 1.0, ic = 0.0;
            int64_t pa = 0;
            if (ts_pcr_window_regress(&h->pcr_window, &sl, &ic, &pa) == 0) {
                h->stc_slope_q64 = (int128_t)(sl * 18446744073709551616.0);
                h->stc_intercept_q64 = (int128_t)(ic * 18446744073709551616.0);
                h->live->pcr_accuracy_ns = (double)pa;
                double instant_drift = (sl - 1.0) * 1000000.0;
                if (instant_drift > 1000000.0)
                    instant_drift = 1000000.0;
                else if (instant_drift < -1000000.0)
                    instant_drift = -1000000.0;
                h->live->pcr_drift_ppm = (h->live->pcr_drift_ppm * 0.9) + (instant_drift * 0.1);
                h->stc_wall_drift_ppm = h->live->pcr_drift_ppm;
            }
            ctx->pcr_count_since_regress = 0;
        }

        if (h->last_pcr_ticks > 0) {
            uint64_t dt_ticks = (pt - h->last_pcr_ticks);
            if (dt_ticks == 0) dt_ticks = 1;
            if (dt_ticks < ((uint64_t)1 << 41)) {
                uint64_t interval_br = (h->pkts_since_pcr * TS_PACKET_BITS * TS_SYSTEM_CLOCK_HZ) / dt_ticks;
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

tsa_plugin_ops_t pcr_ops = {
    .name = "PCR_ANALYSIS",
    .create = pcr_create,
    .destroy = pcr_destroy,
    .get_stream = pcr_get_stream,
};

void tsa_register_pcr_engine(tsa_handle_t* h) {
    extern void tsa_plugin_attach_instance(tsa_handle_t * h, tsa_plugin_ops_t * ops);
    tsa_plugin_attach_instance(h, &pcr_ops);
}
