#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tsa_internal.h"
#include "tsa_plugin.h"

#define TAG "TR101290"

// TR 101 290 Thresholds (Standard DVB)
#define PAT_TIMEOUT_NS 500000000ULL  // 500ms
#define PMT_TIMEOUT_NS 500000000ULL  // 500ms
#define PCR_TIMEOUT_NS 40000000ULL   // 40ms
#define PTS_TIMEOUT_NS 700000000ULL  // 700ms

typedef struct {
    tsa_handle_t* h;
    uint64_t last_pat_check_ns;
    uint64_t last_pmt_check_ns;
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
        tsa_alert_update(h, TSA_ALERT_TRANSPORT, true, "TRANSPORT", pid);
    }

    // 2. Continuity Counter (CC) Check (P1.4)
    if (h->es_tracks[pid].last_cc != 0x10 && !res->has_discontinuity) {
        ts_cc_status_t s = cc_classify_error(h->es_tracks[pid].last_cc, res->cc, res->has_payload,
                                             (pkt[3] & 0x20) && !(pkt[3] & 0x10));

        if (s == TS_CC_LOSS || s == TS_CC_OUT_OF_ORDER) {
            if (!h->es_tracks[pid].ignore_next_cc) {
                tsa_alert_update(h, TSA_ALERT_CC, true, "CC", pid);
            }
        }
    }
    h->es_tracks[pid].last_cc = res->cc;
    h->es_tracks[pid].ignore_next_cc = false;

    // 3. PAT Error Check (P1.3)
    if (now - h->last_pat_ns > PAT_TIMEOUT_NS) {
        tsa_alert_update(h, TSA_ALERT_PAT, true, "PAT_TIMEOUT", 0);
    } else {
        tsa_alert_update(h, TSA_ALERT_PAT, false, "PAT_OK", 0);
    }

    // 4. PMT Error Check (P1.5) - Multi-Program Support
    for (uint32_t i = 0; i < h->program_count; i++) {
        if (h->programs[i].pmt_pid == 0) continue;
        if (now - h->programs[i].last_pmt_ns > PMT_TIMEOUT_NS) {
            tsa_alert_update(h, TSA_ALERT_PMT, true, "PMT_TIMEOUT", h->programs[i].pmt_pid);
        } else {
            tsa_alert_update(h, TSA_ALERT_PMT, false, "PMT_OK", h->programs[i].pmt_pid);
        }
    }

    // 5. PCR Repetition Error Check (P2.3)
    if (res->has_pcr) {
        uint64_t last_pcr_ns = h->es_tracks[pid].last_pcr_ns;
        if (last_pcr_ns > 0) {
            uint64_t diff_ns = now - last_pcr_ns;
            if (diff_ns > PCR_TIMEOUT_NS) {
                tsa_alert_update(h, TSA_ALERT_PCR, true, "PCR_REPETITION", pid);
            } else {
                tsa_alert_update(h, TSA_ALERT_PCR, false, "PCR_OK", pid);
            }
        }
        h->es_tracks[pid].last_pcr_ns = now;
    }

    // 6. PTS Error Check (P2.5)
    if (res->has_pes_header) {
        uint64_t last_pts_vstc = h->es_tracks[pid].last_seen_vstc;
        if (last_pts_vstc > 0) {
            uint64_t diff_ns = h->stc_ns - last_pts_vstc;
            if (diff_ns > PTS_TIMEOUT_NS) {
                tsa_alert_update(h, TSA_ALERT_PTS, true, "PTS_TIMEOUT", pid);
            } else {
                tsa_alert_update(h, TSA_ALERT_PTS, false, "PTS_OK", pid);
            }
        }
        h->es_tracks[pid].last_seen_vstc = h->stc_ns;
    }
}

tsa_plugin_ops_t tr101290_ops = {
    .name = "TR101290_CORE",
    .create = tr_create,
    .destroy = tr_destroy,
    .on_ts = tr_on_ts,
};
