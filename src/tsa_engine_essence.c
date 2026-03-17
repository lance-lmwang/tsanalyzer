#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tsa_internal.h"
#include "tsa_packet_pool.h"
#include "tsa_plugin.h"

#define TAG "ESSENCE"

/* Default T-STD Buffer Sizes (ISO/IEC 13818-1) */
#define TSA_TSTD_TB_SIZE (512 * 8)              /* 512 bytes for TB_x */
#define TSA_TSTD_EB_VIDEO_SIZE (512 * 1024 * 8) /* 512 KB for EB_x (Standard VBV) */
#define TSA_TSTD_EB_AUDIO_SIZE (3584 * 8)       /* 3584 bytes for B_n (Audio) */

#define TSA_NANOS_PER_SEC 1000000000ULL
#define TSA_DEFAULT_RX_BPS 10000000ULL  /* 10 Mbps */
#define TSA_DEFAULT_RBX_BPS 20000000ULL /* 20 Mbps */

typedef struct {
    tsa_handle_t* h;
} essence_ctx_t;

/* Forward declaration for the ops structure */
static void* essence_create(void* parent, void* context_buf);
static void essence_destroy(void* self);
static void essence_on_ts(void* self, const uint8_t* pkt);

tsa_plugin_ops_t essence_plugin_ops = {
    .name = "essence",
    .create = essence_create,
    .destroy = essence_destroy,
    .on_ts = essence_on_ts,
};

/**
 * ISO/IEC 13818-1 T-STD Leakage Logic with EB Backpressure
 */
static void tsa_tstd_update_leak(tsa_handle_t* h, tsa_es_track_t* es, uint64_t now_vstc) {
    if (!h || !es) return;
    if (es->tstd.last_leak_vstc == 0) {
        es->tstd.last_leak_vstc = now_vstc;
        return;
    }

    uint64_t dt = (now_vstc > es->tstd.last_leak_vstc) ? (now_vstc - es->tstd.last_leak_vstc) : 0;
    if (dt == 0) return;

    uint8_t st = es->stream_type;
    bool is_video = tsa_is_video(st);
    bool is_live = (h->live != NULL);

    /* 1. TB Leakage: R_x flows into MB (or EB for non-video)
     * R_rx is tied to the real-time physical bitrate of the TS or synced from level. */
    uint64_t r_x = es->tstd.leak_rate_rx;
    if (r_x == 0 && is_live) r_x = h->live->physical_bitrate_bps;
    if (r_x == 0 && is_live) r_x = (uint64_t)(h->live->pid_bitrate_bps[es->pid] * 6 / 5);
    if (r_x == 0) r_x = TSA_DEFAULT_RX_BPS;

    int128_t tb_leaked_q64 = (((int128_t)r_x * dt) << 64) / TSA_NANOS_PER_SEC;
    if (tb_leaked_q64 > es->tstd.tb_fill_q64) tb_leaked_q64 = es->tstd.tb_fill_q64;
    es->tstd.tb_fill_q64 -= tb_leaked_q64;

    /* 2. EB Capacity & Backpressure Check */
    uint64_t eb_size = is_video ? TSA_TSTD_EB_VIDEO_SIZE : TSA_TSTD_EB_AUDIO_SIZE;
    bool eb_full = es->tstd.eb_fill_q64 >= INT_TO_Q64_64(eb_size);

    if (is_video) {
        /* MB Fills from TB leakage */
        es->tstd.mb_fill_q64 += tb_leaked_q64;

        /* MB Leakage: R_bx flows into EB, but ONLY if EB is not full (Backpressure)
         * R_bx is sync'd from profile/level or estimated as 1.5x PID bitrate. */
        if (!eb_full) {
            uint64_t r_bx = es->tstd.leak_rate_eb;
            if (r_bx == 0 && is_live) r_bx = (uint64_t)(h->live->pid_bitrate_bps[es->pid] * 3 / 2);
            if (r_bx == 0 && is_live) r_bx = (uint64_t)(h->live->physical_bitrate_bps * 4 / 5);
            if (r_bx == 0) r_bx = TSA_DEFAULT_RBX_BPS;

            int128_t mb_leaked_q64 = (((int128_t)r_bx * dt) << 64) / TSA_NANOS_PER_SEC;
            if (mb_leaked_q64 > es->tstd.mb_fill_q64) mb_leaked_q64 = es->tstd.mb_fill_q64;

            /* Check if EB has room for this leak */
            int128_t eb_free_q64 = INT_TO_Q64_64(eb_size) - es->tstd.eb_fill_q64;
            if (mb_leaked_q64 > eb_free_q64) mb_leaked_q64 = eb_free_q64;

            es->tstd.mb_fill_q64 -= mb_leaked_q64;
            es->tstd.eb_fill_q64 += mb_leaked_q64;
        }
    } else {
        /* For Audio: TB leaks to EB, respecting EB size */
        int128_t eb_free_q64 = INT_TO_Q64_64(eb_size) - es->tstd.eb_fill_q64;
        if (tb_leaked_q64 > eb_free_q64) tb_leaked_q64 = eb_free_q64;
        es->tstd.eb_fill_q64 += tb_leaked_q64;
    }

    /* Predictive Buffer Modeling (VBV/CPB) */
    uint64_t eb_fill_bytes = (uint64_t)(es->tstd.eb_fill_q64 >> 64);
    uint64_t r_bx_bytes_sec = (is_video ? es->tstd.leak_rate_eb : es->tstd.leak_rate_rx) / 8;
    if (r_bx_bytes_sec == 0) r_bx_bytes_sec = 1; /* prevent div by zero */

    es->tstd.time_to_overflow_ms = -1;
    es->tstd.time_to_underflow_ms = -1;

    if (eb_size > eb_fill_bytes) {
        uint64_t free_bytes = eb_size - eb_fill_bytes;
        es->tstd.time_to_overflow_ms = (int64_t)((free_bytes * 1000) / r_bx_bytes_sec);

        /* Check if overflow will happen before the next drain (DTS) */
        if (es->au_q.head != es->au_q.tail) {
            uint64_t next_dts = es->au_q.queue[es->au_q.head].dts_ns;
            int64_t time_to_dts_ms = (next_dts > h->stc_ns) ? (int64_t)((next_dts - h->stc_ns) / 1000000) : 0;
            if (es->tstd.time_to_overflow_ms < time_to_dts_ms && es->tstd.time_to_overflow_ms < 500) {
                tsa_push_event(h, TSA_EVENT_TSTD_PREDICTIVE, es->pid, (uint64_t)es->tstd.time_to_overflow_ms);
            }
        }
    } else {
        es->tstd.time_to_overflow_ms = 0;
    }

    if (es->au_q.head != es->au_q.tail) {
        uint64_t next_dts = es->au_q.queue[es->au_q.head].dts_ns;
        uint32_t next_size = es->au_q.queue[es->au_q.head].size;
        int64_t time_to_dts_ms = (next_dts > h->stc_ns) ? (int64_t)((next_dts - h->stc_ns) / 1000000) : 0;

        if (eb_fill_bytes < next_size) {
            uint64_t missing_bytes = next_size - eb_fill_bytes;
            int64_t time_to_fill_ms = (int64_t)((missing_bytes * 1000) / r_bx_bytes_sec);
            if (time_to_fill_ms > time_to_dts_ms) {
                es->tstd.time_to_underflow_ms = time_to_dts_ms;
                if (es->tstd.time_to_underflow_ms < 500) {
                    tsa_push_event(h, TSA_EVENT_TSTD_PREDICTIVE, es->pid, (uint64_t)es->tstd.time_to_underflow_ms);
                }
            }
        }
    }

    es->tstd.last_leak_vstc = now_vstc;
}

static void essence_on_ts(void* self, const uint8_t* pkt) {
    essence_ctx_t* ctx = (essence_ctx_t*)self;
    if (!ctx || !ctx->h) return;
    tsa_handle_t* h = ctx->h;
    const ts_decode_result_t* res = &h->current_res;
    uint16_t pid = res->pid;

    if (pid >= TS_PID_MAX) return;

    tsa_es_track_t* es = &h->es_tracks[pid];

    bool cc_error = h->live ? h->live->latched_cc_error : false;
    if (res->has_discontinuity || cc_error) {
        es->tstd.tb_fill_q64 = 0;
        es->tstd.mb_fill_q64 = 0;
        es->tstd.eb_fill_q64 = 0;
        es->tstd.last_underflow_dts = 0;
        es->pes.last_dts_33 = 0;
        es->au_q.head = 0;
        es->au_q.tail = 0;
        tsa_es_track_clear_accumulator(h, pid);
        return;
    }

    tsa_tstd_update_leak(h, es, h->stc_ns);

    es->tstd.tb_fill_q64 += INT_TO_Q64_64(188 * 8);
    if (es->tstd.tb_fill_q64 > INT_TO_Q64_64(TSA_TSTD_TB_SIZE)) {
        tsa_push_event(h, TSA_EVENT_TSTD_OVERFLOW, pid, (uint64_t)(es->tstd.tb_fill_q64 >> 64));
        tsa_alert_update(h, TSA_ALERT_TSTD, true, "TSTD", pid);
    }

    if (tsa_is_audio(es->stream_type)) {
        void tsa_audio_audit_process_es(tsa_handle_t * h, uint16_t pid, const uint8_t* payload, int len);
        tsa_audio_audit_process_es(h, pid, pkt + 4 + res->af_len, res->payload_len);
    }

    /* Professional Compliance: Check for EB Underflow before and after drain */
    if (es->au_q.head != es->au_q.tail) {
        uint64_t next_dts = es->au_q.queue[es->au_q.head].dts_ns;
        if (h->stc_ns > next_dts) {
            /* Data arrived too late for its DTS deadline */
            if (es->tstd.last_underflow_dts != next_dts) {
                tsa_push_event(h, TSA_EVENT_TSTD_UNDERFLOW, pid, (h->stc_ns - next_dts));
                tsa_alert_update(h, TSA_ALERT_TSTD, true, "TSTD", pid);
                es->tstd.last_underflow_dts = next_dts;
            }
        }
    }

    if (res->payload_len > 0) {
        tsa_es_track_push_packet(h, pid, pkt, res);
    }
    tsa_tstd_drain(h, pid);
}

static void* essence_create(void* parent, void* context_buf) {
    tsa_handle_t* h = (tsa_handle_t*)parent;
    essence_ctx_t* ctx = (essence_ctx_t*)context_buf;
    memset(ctx, 0, sizeof(essence_ctx_t));
    ctx->h = h;
    return ctx;
}

static void essence_destroy(void* self) {
    essence_ctx_t* ctx = (essence_ctx_t*)self;
    if (!ctx || !ctx->h) return;
    tsa_handle_t* h = ctx->h;

    /* Robustness check: Ensure handle members are still valid before cleanup */
    if (!h->pkt_pool || !h->pid_seen || !h->es_tracks) return;

    /* Securely cleanup PES references in the packet pool */
    for (int i = 0; i < TS_PID_MAX; i++) {
        tsa_es_track_clear_accumulator(h, i);
    }
}