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

typedef struct {
    tsa_handle_t* h;
    tsa_stream_t stream;
} essence_ctx_t;

/* Forward declaration for the ops structure */
static void* essence_create(void* parent, void* context_buf);
static void essence_destroy(void* self);
static tsa_stream_t* essence_get_stream(void* self);
static void essence_on_ts(void* self, const uint8_t* pkt);

tsa_plugin_ops_t essence_plugin_ops = {
    .name = "essence", .create = essence_create, .destroy = essence_destroy, .get_stream = essence_get_stream, };

/**
 * ISO/IEC 13818-1 T-STD Leakage Logic with EB Backpressure
 */
static void tsa_tstd_update_leak(tsa_handle_t* h, tsa_es_track_t* es, uint64_t now_vstc) {
    if (es->tstd.last_leak_vstc == 0) {
        es->tstd.last_leak_vstc = now_vstc;
        return;
    }

    uint64_t dt = (now_vstc > es->tstd.last_leak_vstc) ? (now_vstc - es->tstd.last_leak_vstc) : 0;
    if (dt == 0) return;

    /* Q64.64 Precision factor for (bits/s * ns) -> bits */
    double factor = (18446744073709551616.0 / 1000000000.0);
    uint8_t st = es->stream_type;
    bool is_video = tsa_is_video(st);

    /* 1. TB Leakage: R_x flows into MB (or EB for non-video) */
    uint64_t r_x = (uint64_t)(h->live->pid_bitrate_bps[es->pid] * 1.2);
    if (r_x == 0) r_x = 10000000;

    int128_t tb_leaked_q64 = (int128_t)r_x * dt * factor;
    if (tb_leaked_q64 > es->tstd.tb_fill_q64) tb_leaked_q64 = es->tstd.tb_fill_q64;
    es->tstd.tb_fill_q64 -= tb_leaked_q64;

    /* 2. EB Capacity & Backpressure Check */
    uint64_t eb_size = is_video ? TSA_TSTD_EB_VIDEO_SIZE : TSA_TSTD_EB_AUDIO_SIZE;
    bool eb_full = es->tstd.eb_fill_q64 >= INT_TO_Q64_64(eb_size);

    if (is_video) {
        /* MB Fills from TB leakage */
        es->tstd.mb_fill_q64 += tb_leaked_q64;

        /* MB Leakage: R_bx flows into EB, but ONLY if EB is not full (Backpressure) */
        if (!eb_full) {
            uint64_t r_bx = (uint64_t)(h->live->pid_bitrate_bps[es->pid] * 1.5);
            if (r_bx == 0) r_bx = 20000000;

            int128_t mb_leaked_q64 = (int128_t)r_bx * dt * factor;
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

    es->tstd.last_leak_vstc = now_vstc;
}

static void essence_on_ts(void* self, const uint8_t* pkt) {
    essence_ctx_t* ctx = (essence_ctx_t*)self;
    tsa_handle_t* h = ctx->h;
    const ts_decode_result_t* res = &h->current_res;
    uint16_t pid = res->pid;
    tsa_es_track_t* es = &h->es_tracks[pid];

    if (res->has_discontinuity || h->live->latched_cc_error) {
        es->tstd.tb_fill_q64 = 0;
        es->tstd.mb_fill_q64 = 0;
        es->tstd.eb_fill_q64 = 0;
        for (uint32_t i = 0; i < es->pes.ref_count; i++) {
            tsa_packet_unref(h->pkt_pool, es->pes.refs[i]);
        }
        es->pes.ref_count = 0;
        es->pes.total_length = 0;
        return;
    }

    tsa_tstd_update_leak(h, es, h->stc_ns);

    es->tstd.tb_fill_q64 += INT_TO_Q64_64(188 * 8);
    if (es->tstd.tb_fill_q64 > INT_TO_Q64_64(TSA_TSTD_TB_SIZE)) {
        tsa_push_event(h, TSA_EVENT_TSTD_OVERFLOW, pid, (uint64_t)(es->tstd.tb_fill_q64 >> 64));
    }

    if (res->payload_len > 0) {
        tsa_packet_t* p_obj = (tsa_packet_t*)((uint8_t*)pkt - offsetof(tsa_packet_t, data));
        if (res->pusi) {
            if (es->pes.ref_count > 0) {
                tsa_handle_es_payload(h, pid, NULL, es->pes.total_length, h->stc_ns);
                for (uint32_t i = 0; i < es->pes.ref_count; i++) {
                    tsa_packet_unref(h->pkt_pool, es->pes.refs[i]);
                }
            }
            es->pes.ref_count = 0;
            es->pes.total_length = 0;
            if (res->has_pes_header) {
                es->pes.pending_dts_ns = (res->dts * 1000000ULL) / 90;
                es->pes.last_pts_33 = res->pts;
                es->pes.last_dts_33 = res->dts;
                es->pes.has_pts = res->pts != 0;
                es->pes.has_dts = res->dts != 0;
            }
        }
        if (es->pes.ref_count < TSA_PES_MAX_REFS) {
            es->pes.refs[es->pes.ref_count] = p_obj;
            es->pes.payload_offsets[es->pes.ref_count] = (uint16_t)(pkt + 4 + res->af_len - (uint8_t*)p_obj->data);
            es->pes.payload_lens[es->pes.ref_count] = (uint8_t)res->payload_len;
            es->pes.ref_count++;
            es->pes.total_length += res->payload_len;
            tsa_packet_ref(p_obj);
        }
    }
    tsa_tstd_drain(h, pid);
}

static void* essence_create(void* parent, void* context_buf) {
    (void)context_buf;
    tsa_handle_t* h = (tsa_handle_t*)parent;
    essence_ctx_t* ctx = calloc(1, sizeof(essence_ctx_t));
    ctx->h = h;
    tsa_stream_init(&ctx->stream, ctx, essence_on_ts);
    return ctx;
}

static void essence_destroy(void* self) {
    essence_ctx_t* ctx = (essence_ctx_t*)self;
    tsa_stream_destroy(&ctx->stream);
    free(ctx);
}

static tsa_stream_t* essence_get_stream(void* self) {
    essence_ctx_t* ctx = (essence_ctx_t*)self;
    return &ctx->stream;
}
