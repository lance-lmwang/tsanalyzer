#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tsa_internal.h"
#include "tsa_engine.h"

typedef struct {
    tsa_handle_t* h;
} tr101290_ctx_t;

static void* tr_create(void* h) {
    tr101290_ctx_t* ctx = calloc(1, sizeof(tr101290_ctx_t));
    ctx->h = (tsa_handle_t*)h;
    return ctx;
}

static void tr_destroy(void* engine) {
    free(engine);
}

static void tr_process_packet(void* engine, const uint8_t* pkt, const void* decode_res, uint64_t now) {
    tr101290_ctx_t* ctx = (tr101290_ctx_t*)engine;
    tsa_handle_t* h = ctx->h;
    const ts_decode_result_t* res = (const ts_decode_result_t*)decode_res;
    uint16_t pid = res->pid;

    // 1. Transport Error Check
    if (pkt[1] & 0x80) {
        h->live->transport_error.count++;
        h->live->transport_error.last_timestamp_ns = now;
    }

    // 2. Continuity Counter (CC) Check
    if (h->live->pid_packet_count[pid] > 1 && !res->has_discontinuity) {
        if (h->ignore_next_cc[pid]) {
            h->ignore_next_cc[pid] = false;
        } else {
            ts_cc_status_t s = cc_classify_error(h->last_cc[pid], res->cc, res->has_payload, (pkt[3] & 0x20) && !(pkt[3] & 0x10));
            
            if (s == TS_CC_LOSS) {
                h->pid_cc_error_suppression[pid]++;
                if (h->pid_cc_error_suppression[pid] >= 3) { 
                    if (h->live->cc_error.count == 0) h->live->cc_error.first_timestamp_ns = now;
                    h->live->cc_error.count++;
                    h->live->cc_error.last_timestamp_ns = now;
                    h->live->cc_error.triggering_vstc = h->stc_ns;
                    h->live->cc_error.absolute_byte_offset = h->live->total_ts_packets * 188;
                    h->live->pid_cc_errors[pid]++;
                    h->live->latched_cc_error = 1;
                    h->pid_status[pid] = TSA_STATUS_DEGRADED;
                    h->live->cc_loss_count += (res->cc - ((h->last_cc[pid] + 1) & 0x0F)) & 0x0F;
                    tsa_push_event(h, TSA_EVENT_CC_ERROR, pid, (uint64_t)res->cc);
                }
            } else if (s == TS_CC_DUPLICATE) {
                h->live->cc_duplicate_count++;
            } else if (s == TS_CC_OUT_OF_ORDER) {
                if (h->live->cc_error.count == 0) h->live->cc_error.first_timestamp_ns = now;
                h->live->cc_error.count++;
                h->live->cc_error.last_timestamp_ns = now;
                h->pid_status[pid] = TSA_STATUS_DEGRADED;
                tsa_push_event(h, TSA_EVENT_CC_ERROR, pid, (uint64_t)res->cc);
            } else {
                if (h->live->pid_packet_count[pid] % 100 == 0) {
                    h->pid_cc_error_suppression[pid] = 0;
                }
            }
        }
    }
    h->last_cc[pid] = res->cc;
}

static tsa_engine_ops_t tr101290_ops = {
    .name = "TR101290_CORE",
    .create = tr_create,
    .destroy = tr_destroy,
    .process_packet = tr_process_packet,
};

void tsa_register_tr101290_engine(tsa_handle_t* h) {
    tsa_register_engine(h, &tr101290_ops);
}
