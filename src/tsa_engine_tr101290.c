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

static bool tsa_debounce_update(tsa_debounce_t* d, bool active, uint64_t now, uint64_t fire_ns, uint64_t resolve_ns) {
    if (active) {
        if (d->last_absence_ns == 0) d->last_absence_ns = now;
        d->last_occurrence_ns = now;
        if (!d->is_fired && (now - d->last_absence_ns >= fire_ns)) {
            d->is_fired = true;
            d->fired_time_ns = now;
        }
    } else {
        d->last_absence_ns = now;
        if (d->is_fired && d->last_occurrence_ns > 0 && (now - d->last_occurrence_ns >= resolve_ns)) {
            d->is_fired = false;
        }
        if (!d->is_fired) {
            d->last_absence_ns = now;
            // If not fired and not active, we haven't seen an error yet, so reset absence tracker
            // to now to ensure fire_ns starts from the next 'active' event.
        }
    }
    return d->is_fired;
}

static void* tr_create(void* h, void* context_buf) {
    tr101290_ctx_t* ctx = (tr101290_ctx_t*)context_buf;
    memset(ctx, 0, sizeof(tr101290_ctx_t));
    ctx->h = (tsa_handle_t*)h;
    tsa_stream_init(&ctx->stream, ctx, tr_on_ts);
    return ctx;
}

static void tr_destroy(void* engine) {
    tr101290_ctx_t* ctx = (tr101290_ctx_t*)engine;
    tsa_stream_destroy(&ctx->stream);
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
    bool transport_error_active = (pkt[1] & 0x80) != 0;
    if (tsa_debounce_update(&h->debounce_transport, transport_error_active, now, 100000000ULL, 2000000000ULL)) {
        if (h->live->transport_error.count == 0) h->live->transport_error.first_timestamp_ns = now;
        h->live->transport_error.count++;
        h->live->transport_error.last_timestamp_ns = now;
        tsa_push_event(h, TSA_EVENT_TRANSPORT_ERROR, pid, 0);
    }

    // 2. Continuity Counter (CC) Check
    bool cc_error_detected = false;
    if (h->es_tracks[pid].last_cc != 0x10 && !res->has_discontinuity) {
        ts_cc_status_t s = cc_classify_error(h->es_tracks[pid].last_cc, res->cc, res->has_payload,
                                             (pkt[3] & 0x20) && !(pkt[3] & 0x10));

        if (s == TS_CC_LOSS || s == TS_CC_OUT_OF_ORDER) {
            cc_error_detected = true;
            if (s == TS_CC_LOSS) {
                h->live->cc_loss_count += (res->cc - ((h->es_tracks[pid].last_cc + 1) & 0x0F)) & 0x0F;
            }
        }
    }

    // CC Debounce: Sustained errors (>100ms of CC errors within 500ms windows)
    bool cc_currently_active = (now - h->debounce_cc.last_occurrence_ns < 500000000ULL);
    if (cc_error_detected) cc_currently_active = true;

    if (tsa_debounce_update(&h->debounce_cc, cc_currently_active, now, 100000000ULL, 2000000000ULL)) {
        if (!h->es_tracks[pid].ignore_next_cc) {
            if (h->live->cc_error.count == 0) h->live->cc_error.first_timestamp_ns = now;
            h->live->cc_error.count++;
            h->live->cc_error.last_timestamp_ns = now;
            h->live->cc_error.triggering_vstc = h->stc_ns;
            h->live->cc_error.absolute_byte_offset = h->live->total_ts_packets * 188;
            h->live->pid_cc_errors[pid]++;
            h->live->latched_cc_error = 1;
            h->pid_status[pid] = TSA_STATUS_DEGRADED;
            tsa_push_event(h, TSA_EVENT_CC_ERROR, pid, (uint64_t)res->cc);
        }
    }
    h->es_tracks[pid].ignore_next_cc = false;

    // 3. PTS Error Check (P2.5)
    bool pts_error_detected = false;
    uint64_t pts_diff_ms = 0;
    if (res->has_pes_header) {
        uint64_t last_pts_ns = h->es_tracks[pid].last_seen_vstc;
        if (last_pts_ns > 0) {
            uint64_t diff_ns = h->stc_ns - last_pts_ns;
            if (diff_ns > 700000000ULL) {  // 700ms threshold
                pts_error_detected = true;
                pts_diff_ms = diff_ns / 1000000ULL;
            }
        }
        h->es_tracks[pid].last_seen_vstc = h->stc_ns;
    }

    bool pts_active = (now - h->debounce_pts.last_occurrence_ns < 1000000000ULL);
    if (pts_error_detected) pts_active = true;

    if (tsa_debounce_update(&h->debounce_pts, pts_active, now, 100000000ULL, 2000000000ULL)) {
        if (h->live->pts_error.count == 0) h->live->pts_error.first_timestamp_ns = now;
        h->live->pts_error.count++;
        h->live->pts_error.last_timestamp_ns = now;
        tsa_push_event(h, TSA_EVENT_PTS_ERROR, pid, pts_diff_ms);
    }

    h->es_tracks[pid].last_cc = res->cc;
}

tsa_plugin_ops_t tr101290_ops = {
    .name = "TR101290_CORE",
    .create = tr_create,
    .destroy = tr_destroy,
    .get_stream = tr_get_stream,
};
