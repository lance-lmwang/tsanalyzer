#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tsa_internal.h"
#include "tsa_plugin.h"

#define TAG "METROLOGY"

typedef struct {
    tsa_handle_t* h;
    uint32_t pcr_count_since_regress;
} pcr_ctx_t;

static void pcr_on_ts(void* self, const uint8_t* pkt);

static void* pcr_create(void* h, void* context_buf) {
    pcr_ctx_t* ctx = (pcr_ctx_t*)context_buf;
    memset(ctx, 0, sizeof(pcr_ctx_t));
    ctx->h = (tsa_handle_t*)h;
    return ctx;
}

static void pcr_destroy(void* engine) {
    (void)engine;
}

static void pcr_on_ts(void* self, const uint8_t* pkt) {
    pcr_ctx_t* ctx = (pcr_ctx_t*)self;
    tsa_handle_t* h = ctx->h;
    const ts_decode_result_t* res = &h->current_res;
    uint64_t now = h->current_ns;
    uint16_t pid = res->pid;

    if (res->af_len >= 6 && (pkt[5] & 0x10)) {
        uint64_t pcr_base = ((uint64_t)pkt[6] << 25) | ((uint64_t)pkt[7] << 17) | ((uint64_t)pkt[8] << 9) |
                            ((uint64_t)pkt[9] << 1) | ((uint64_t)pkt[10] >> 7);
        uint16_t pcr_ext = ((uint16_t)(pkt[10] & 0x01) << 8) | pkt[11];
        uint64_t pcr_ticks = pcr_base * 300 + pcr_ext;

        /* Standardized Metrology Update */
        tsa_pcr_track_update(&h->pcr_tracks[pid], pcr_ticks, now, h->live->pid_packet_count[pid],
                             h->live->total_ts_packets, h->config.op_mode == TSA_MODE_LIVE);

        if (h->master_pcr_pid == 0x1FFF || h->master_pcr_pid == pid) {
            if (h->master_pcr_pid == 0x1FFF) {
                tsa_info(TAG, "Locking global STC to Master PCR PID 0x%04x", pid);
                h->master_pcr_pid = pid;
            }
            h->last_pcr_ticks = pcr_ticks;
            h->stc_locked = true;
            h->stc_wall_drift_ppm = h->pcr_tracks[pid].drift_ppm;
        }
    }
}

tsa_plugin_ops_t pcr_ops = {
    .name = "PCR_ANALYZER",
    .create = pcr_create,
    .destroy = pcr_destroy,
    .on_ts = pcr_on_ts,
};
