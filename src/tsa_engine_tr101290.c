#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tsa_internal.h"
#include "tsa_plugin.h"

typedef struct {
    tsa_handle_t* h;
    tsa_stream_t stream;
} tr101290_ctx_t;

static void tr_on_ts(void* self, const uint8_t* pkt);

static void* tr_create(void* h) {
    tr101290_ctx_t* ctx = calloc(1, sizeof(tr101290_ctx_t));
    ctx->h = (tsa_handle_t*)h;
    tsa_stream_init(&ctx->stream, ctx, tr_on_ts);
    return ctx;
}

static void tr_destroy(void* engine) {
    tr101290_ctx_t* ctx = (tr101290_ctx_t*)engine;
    tsa_stream_destroy(&ctx->stream);
    free(ctx);
}

static tsa_stream_t* tr_get_stream(void* engine) {
    tr101290_ctx_t* ctx = (tr101290_ctx_t*)engine;
    return &ctx->stream;
}

static void tr_on_ts(void* self, const uint8_t* pkt) {
    tr101290_ctx_t* ctx = (tr101290_ctx_t*)self;
    tsa_handle_t* h = ctx->h;
    const ts_decode_result_t* res = &h->current_res;
    uint64_t now = h->current_ns;
    uint16_t pid = res->pid;

    // 1. Transport Error Check
    if (pkt[1] & 0x80) {
        if (h->live->transport_error.count == 0) h->live->transport_error.first_timestamp_ns = now;
        h->live->transport_error.count++;
        h->live->transport_error.last_timestamp_ns = now;
        tsa_push_event(h, TSA_EVENT_TRANSPORT_ERROR, pid, 0);
    }

    // 2. Continuity Counter (CC) Check
    if (h->last_cc[pid] != 0x10 && !res->has_discontinuity) {
        ts_cc_status_t s = cc_classify_error(h->last_cc[pid], res->cc, res->has_payload, (pkt[3] & 0x20) && !(pkt[3] & 0x10));

        if (s == TS_CC_LOSS || s == TS_CC_OUT_OF_ORDER) {
            if (!h->ignore_next_cc[pid]) {
                if (h->live->cc_error.count == 0) h->live->cc_error.first_timestamp_ns = now;
                h->live->cc_error.count++;
                h->live->cc_error.last_timestamp_ns = now;
                h->live->cc_error.triggering_vstc = h->stc_ns;
                h->live->cc_error.absolute_byte_offset = h->live->total_ts_packets * 188;
                h->live->pid_cc_errors[pid]++;
                h->live->latched_cc_error = 1;
                h->pid_status[pid] = TSA_STATUS_DEGRADED;

                if (s == TS_CC_LOSS) {
                    h->live->cc_loss_count += (res->cc - ((h->last_cc[pid] + 1) & 0x0F)) & 0x0F;
                }
                tsa_push_event(h, TSA_EVENT_CC_ERROR, pid, (uint64_t)res->cc);
            }
        }
        h->ignore_next_cc[pid] = false;
    }
    // 3. PTS Error Check (P2.5)
    if (res->has_pes_header) {
        uint64_t last_pts_ns = h->pid_last_seen_vstc[pid];
        if (last_pts_ns > 0) {
            uint64_t diff_ns = h->stc_ns - last_pts_ns;
            if (diff_ns > 700000000ULL) { // 700ms threshold
                if (h->live->pts_error.count == 0) h->live->pts_error.first_timestamp_ns = now;
                h->live->pts_error.count++;
                h->live->pts_error.last_timestamp_ns = now;
                tsa_push_event(h, TSA_EVENT_PTS_ERROR, pid, diff_ns / 1000000ULL);
            }
        }
        h->pid_last_seen_vstc[pid] = h->stc_ns;
    }

    h->last_cc[pid] = res->cc;
}

tsa_plugin_ops_t tr101290_ops = {
    .name = "TR101290_CORE",
    .create = tr_create,
    .destroy = tr_destroy,
    .get_stream = tr_get_stream,
};

void tsa_register_tr101290_engine(tsa_handle_t* h) {
    extern void tsa_plugin_attach_instance(tsa_handle_t* h, tsa_plugin_ops_t* ops);
    tsa_plugin_attach_instance(h, &tr101290_ops);
}
