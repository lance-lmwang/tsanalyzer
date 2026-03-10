#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tsa_internal.h"
#include "tsa_plugin.h"

#define TAG "TR101290"

typedef struct {
    tsa_handle_t* h;
} tr101290_ctx_t;

static void tr_on_ts(void* self, const uint8_t* pkt);

static void* tr_create(void* h, void* context_buf) {
    tr101290_ctx_t* ctx = (tr101290_ctx_t*)context_buf;
    memset(ctx, 0, sizeof(tr101290_ctx_t));
    ctx->h = (tsa_handle_t*)h;
    return ctx;
}

static void tr_destroy(void* engine) {
    (void)engine;
}

static void tr_on_ts(void* self, const uint8_t* pkt) {
    tr101290_ctx_t* ctx = (tr101290_ctx_t*)self;
    tsa_handle_t* h = ctx->h;
    const ts_decode_result_t* res = &h->current_res;
    uint64_t now = h->current_ns;
    uint16_t pid = res->pid;

    // 1. Transport Error Check (P2.1)
    if (pkt[1] & 0x80) {
        if (h->live->transport_error.count == 0) h->live->transport_error.first_timestamp_ns = now;
        h->live->transport_error.count++;
        h->live->transport_error.last_timestamp_ns = now;
        tsa_alert_update(h, TSA_ALERT_TRANSPORT, true, "TRANSPORT", pid);
    }

    // 2. Continuity Counter (CC) Check (P1.4)
    if (h->es_tracks[pid].last_cc != 0x10 && !res->has_discontinuity) {
        ts_cc_status_t s = cc_classify_error(h->es_tracks[pid].last_cc, res->cc, res->has_payload,
                                             (pkt[3] & 0x20) && !(pkt[3] & 0x10));

        if (s == TS_CC_LOSS || s == TS_CC_OUT_OF_ORDER) {
            if (s == TS_CC_LOSS) {
                h->live->cc_loss_count += (res->cc - ((h->es_tracks[pid].last_cc + 1) & 0x0F)) & 0x0F;
            }

            if (!h->es_tracks[pid].ignore_next_cc) {
                if (h->live->cc_error.count == 0) h->live->cc_error.first_timestamp_ns = now;
                h->live->cc_error.count++;
                h->live->cc_error.last_timestamp_ns = now;
                h->live->cc_error.triggering_vstc = h->stc_ns;
                h->live->cc_error.absolute_byte_offset = h->current_packet_offset;
                h->live->pid_cc_errors[pid]++;
                h->live->latched_cc_error = 1;
                h->es_tracks[pid].status = TSA_STATUS_DEGRADED;

                tsa_alert_update(h, TSA_ALERT_CC, true, "CC", pid);
            }
        }
    }
    h->es_tracks[pid].ignore_next_cc = false;

    // 3. PTS Error Check (P2.5)
    if (res->has_pes_header) {
        uint64_t last_pts_ns = h->es_tracks[pid].last_seen_vstc;
        if (last_pts_ns > 0) {
            uint64_t diff_ns = h->stc_ns - last_pts_ns;
            if (diff_ns > 700000000ULL) { /* 700ms threshold according to TR 101 290 */
                if (h->live->pts_error.count == 0) h->live->pts_error.first_timestamp_ns = now;
                h->live->pts_error.count++;
                h->live->pts_error.last_timestamp_ns = now;
                tsa_alert_update(h, TSA_ALERT_PTS, true, "PTS", pid);
            }
        }
        h->es_tracks[pid].last_seen_vstc = h->stc_ns;
    }

    h->es_tracks[pid].last_cc = res->cc;
}

tsa_plugin_ops_t tr101290_ops = {
    .name = "TR101290_CORE",
    .create = tr_create,
    .destroy = tr_destroy,
    .on_ts = tr_on_ts,
};
