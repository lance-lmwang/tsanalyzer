#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tsa_internal.h"
#include "tsa_plugin.h"

#define TAG "METROLOGY"

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

    /* Standardized PCR Processing:
     * PCR_PID = 0x1FFF means the program has no PCR, so we ignore it.
     * We process every packet passing through this engine to maintain
     * accurate packet counts between PCR samples. However, we only trigger
     * regression and repetition checks on packets actually carrying a PCR field. */
    if (pid == 0x1FFF) return;

    uint64_t pt = tsa_pkt_get_pcr(pkt);
    bool is_master = (h->master_pcr_pid == pid);

    if (pt != INVALID_PCR || is_master) {
        /* Use global counters which include ALL packets (including 0x1FFF)
         * as incremented in tsa_process_packet() before reaching this plugin. */
        uint64_t pid_pkts_now = h->live->pid_packet_count[pid];
        uint64_t total_pkts_now = h->live->total_ts_packets;

        /* Update PID reference status */
        h->live->pid_is_referenced[pid] = true;
        tsa_update_pid_tracker(h, pid);

        /* 1. Core Metrology Update (Uses the standard ISO formula internally) */
        tsa_pcr_track_update(&h->pcr_tracks[pid], pt, now, pid_pkts_now, total_pkts_now,
                             h->config.op_mode == TSA_MODE_LIVE);

        /* 2. Export Metrics to h->live */
        h->live->pcr_jitter_max_ns = (uint64_t)(fabs(h->pcr_tracks[pid].pcr_jitter_ms) * 1000000.0);
        h->live->pcr_repetition_error.count = h->pcr_tracks[pid].priority_1_errors;

        if (h->pcr_tracks[pid].bitrate_bps > 0) {
            h->live->pid_bitrate_bps[pid] = h->pcr_tracks[pid].bitrate_bps;
        }

        /* 3. Master STC Handling (for global pacing/MDI) */
        if (h->master_pcr_pid != 0x1FFF) {
            if (now > h->live->pid_last_seen_ns[h->master_pcr_pid] &&
                (now - h->live->pid_last_seen_ns[h->master_pcr_pid]) > 5000000000ULL) {
                tsa_warn(TAG, "Master PCR PID 0x%04x disappeared for > 5s, releasing lock", h->master_pcr_pid);
                h->master_pcr_pid = 0x1FFF;
                h->stc_locked = false;
            }
        }

        if (h->master_pcr_pid == 0x1FFF && pt != INVALID_PCR) {
            h->master_pcr_pid = pid;
            tsa_info(TAG, "Locking global STC to Master PCR PID 0x%04x", pid);
        }

        if (h->master_pcr_pid == pid && pt != INVALID_PCR) {
            uint64_t pcr_ns = tsa_pcr_to_ns(pt);
            if (!h->stc_locked) {
                h->stc_ns = pcr_ns;
                h->stc_locked = true;
            }

            if (h->pcr_tracks[pid].clock.locked) {
                h->stc_slope_q64 = (int128_t)(h->pcr_tracks[pid].clock.slope * 18446744073709551616.0);
                h->stc_intercept_q64 = (int128_t)(h->pcr_tracks[pid].clock.intercept * 18446744073709551616.0);
                h->live->pcr_accuracy_ns = (double)(h->pcr_tracks[pid].pcr_jitter_ms * 1000000.0);
                h->live->pcr_drift_ppm = h->pcr_tracks[pid].drift_ppm;
                h->stc_wall_drift_ppm = h->pcr_tracks[pid].drift_ppm;
            }
        }
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
