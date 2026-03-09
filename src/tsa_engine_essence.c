#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tsa_internal.h"
#include "tsa_packet_pool.h"
#include "tsa_plugin.h"

#define TAG "ESSENCE"

typedef struct {
    tsa_handle_t* h;
    tsa_stream_t stream;
} essence_ctx_t;

static void essence_on_ts(void* self, const uint8_t* pkt);

static void* essence_create(void* h, void* context_buf) {
    essence_ctx_t* ctx = (essence_ctx_t*)context_buf;
    memset(ctx, 0, sizeof(essence_ctx_t));
    ctx->h = (tsa_handle_t*)h;
    tsa_stream_init(&ctx->stream, ctx, essence_on_ts);
    return ctx;
}

static void essence_destroy(void* engine) {
    essence_ctx_t* ctx = (essence_ctx_t*)engine;
    tsa_stream_destroy(&ctx->stream);
}

static tsa_stream_t* essence_get_stream(void* engine) {
    essence_ctx_t* ctx = (essence_ctx_t*)engine;
    return &ctx->stream;
}

/**
 * ISO/IEC 13818-1 T-STD Leakage Logic
 */
static void tsa_tstd_update_leak(tsa_handle_t* h, tsa_es_track_t* es, uint64_t now_vstc) {
    if (es->tstd.last_leak_vstc == 0) {
        es->tstd.last_leak_vstc = now_vstc;
        return;
    }

    uint64_t dt = (now_vstc > es->tstd.last_leak_vstc) ? (now_vstc - es->tstd.last_leak_vstc) : 0;
    if (dt == 0) return;

    /* 1. TB Leakage: R_px is the total TS bitrate */
    uint64_t r_px = h->live->total_bitrate_bps;
    if (r_px == 0) r_px = 10000000;  // Default fallback

    int128_t tb_leaked_q64 = (int128_t)r_px * dt * (18446744073709551616.0 / 1000000000.0);
    if (es->tstd.tb_fill_q64 > tb_leaked_q64)
        es->tstd.tb_fill_q64 -= tb_leaked_q64;
    else
        es->tstd.tb_fill_q64 = 0;

    /* 2. MB Leakage: R_bx is the peak video bitrate */
    if (tsa_is_video(h->pid_stream_type[es->pid])) {
        uint64_t r_bx = (uint64_t)(h->live->pid_bitrate_bps[es->pid] * 1.5);
        if (r_bx == 0) r_bx = 5000000;

        int128_t mb_leaked_q64 = (int128_t)r_bx * dt * (18446744073709551616.0 / 1000000000.0);
        if (es->tstd.mb_fill_q64 > mb_leaked_q64)
            es->tstd.mb_fill_q64 -= mb_leaked_q64;
        else
            es->tstd.mb_fill_q64 = 0;
    }

    es->tstd.last_leak_vstc = now_vstc;
}

static void essence_on_ts(void* self, const uint8_t* pkt) {
    essence_ctx_t* ctx = (essence_ctx_t*)self;
    tsa_handle_t* h = ctx->h;
    const ts_decode_result_t* res = &h->current_res;
    uint16_t pid = res->pid;
    tsa_es_track_t* es = &h->es_tracks[pid];

    /* T-STD Step 0: Robustness & Reset on continuity issues */
    if (res->has_discontinuity || h->live->latched_cc_error) {
        es->tstd.tb_fill_q64 = 0;
        es->tstd.mb_fill_q64 = 0;
        es->tstd.eb_fill_q64 = 0;

        /* Cleanup PES refs on reset */
        for (uint32_t i = 0; i < es->pes.ref_count; i++) {
            tsa_packet_unref(h->pkt_pool, es->pes.refs[i]);
        }
        es->pes.ref_count = 0;
        es->pes.total_length = 0;
        return;
    }

    /* T-STD Step 1: Update Leakage for TB and MB */
    tsa_tstd_update_leak(h, es, h->stc_ns);

    /* T-STD Step 2: Feed TB (all 188 bytes) */
    es->tstd.tb_fill_q64 += INT_TO_Q64_64(188 * 8);

    /* Check TB Overflow (ISO: 512 bytes for TB_x) */
    if (es->tstd.tb_fill_q64 > INT_TO_Q64_64(512 * 8)) {
        tsa_push_event(h, TSA_EVENT_TSTD_OVERFLOW, pid, (uint64_t)(es->tstd.tb_fill_q64 >> 64));
    }

    if (res->payload_len > 0) {
        /* Derive packet object for Zero-Copy referencing */
        tsa_packet_t* p_obj = (tsa_packet_t*)((uint8_t*)pkt - offsetof(tsa_packet_t, data));

        if (res->pusi) {
            /* PES Finalization - Hand off to sniffer before clearing */
            if (es->pes.ref_count > 0) {
                tsa_handle_es_payload(h, pid, NULL, es->pes.total_length, h->stc_ns);

                /* Release all references */
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

        /* T-STD Step 3: Feed MB/EB and accumulate PES references */
        if (es->pes.ref_count < TSA_PES_MAX_REFS) {
            es->pes.refs[es->pes.ref_count] = p_obj;
            es->pes.payload_offsets[es->pes.ref_count] = (uint16_t)(pkt + 4 + res->af_len - (uint8_t*)p_obj->data);
            es->pes.payload_lens[es->pes.ref_count] = (uint8_t)res->payload_len;
            es->pes.ref_count++;
            es->pes.total_length += res->payload_len;

            /* Pin packet in memory */
            tsa_packet_ref(p_obj);

            /* MB/EB Fill logic */
            es->tstd.mb_fill_q64 += INT_TO_Q64_64(res->payload_len * 8);
            es->tstd.eb_fill_q64 += INT_TO_Q64_64(res->payload_len * 8);
        }
    }

    /* Step 4: Instantaneous EB Drain (Simulated by external timer or decoder trigger) */
    tsa_tstd_drain(h, pid);
}

tsa_plugin_ops_t essence_ops = {
    .name = "ESSENCE_ANALYSIS",
    .create = essence_create,
    .destroy = essence_destroy,
    .get_stream = essence_get_stream,
};

void tsa_register_essence_engine(tsa_handle_t* h) {
    extern void tsa_plugin_attach_instance(tsa_handle_t * h, tsa_plugin_ops_t * ops);
    tsa_plugin_attach_instance(h, &essence_ops);
}
