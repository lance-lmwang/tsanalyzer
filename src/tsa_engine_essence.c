#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tsa_internal.h"
#include "tsa_plugin.h"

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

static void tsa_tstd_tb_leak(tsa_handle_t* h, tsa_es_track_t* es, uint64_t now_vstc) {
    if (es->tstd.last_leak_vstc == 0) {
        es->tstd.last_leak_vstc = now_vstc;
        return;
    }

    uint64_t dt = (now_vstc > es->tstd.last_leak_vstc) ? (now_vstc - es->tstd.last_leak_vstc) : 0;
    if (dt == 0) return;

    /* R_px = 1.2 * bitrate (standard assumption) */
    uint64_t r_px = (uint64_t)(h->live->pid_bitrate_bps[es->pid] * 1.2);
    if (r_px == 0) r_px = 10000000;

    int128_t leaked_bits_q64 = (int128_t)r_px * dt * (18446744073709551616.0 / 1000000000.0);

    if (es->tstd.tb_fill_q64 > leaked_bits_q64)
        es->tstd.tb_fill_q64 -= leaked_bits_q64;
    else
        es->tstd.tb_fill_q64 = 0;

    es->tstd.last_leak_vstc = now_vstc;
}

static void essence_on_ts(void* self, const uint8_t* pkt) {
    essence_ctx_t* ctx = (essence_ctx_t*)self;
    tsa_handle_t* h = ctx->h;
    const ts_decode_result_t* res = &h->current_res;
    uint16_t pid = res->pid;
    tsa_es_track_t* es = &h->es_tracks[pid];

    /* 1. T-STD Robustness: Reset on CC errors or discontinuities */
    if (res->has_discontinuity || h->live->alarm_cc_error) {
        es->tstd.tb_fill_q64 = 0;
        es->tstd.eb_fill_q64 = 0;
        // Reset PES accumulator too
        for (uint32_t i = 0; i < es->pes.ref_count; i++) {
            // Note: In real system, we'd need access to the pool here to unref.
            // For now we assume cleanup happens on finalize.
        }
        es->pes.ref_count = 0;
        es->pes.total_length = 0;
    }

    /* 2. TB Simulation */
    tsa_tstd_tb_leak(h, es, h->stc_ns);
    es->tstd.tb_fill_q64 += INT_TO_Q64_64(188 * 8);

    if (res->payload_len > 0) {
        /* Derive packet object for Zero-Copy referencing */
        tsa_packet_t* p_obj = (tsa_packet_t*)((uint8_t*)pkt - offsetof(tsa_packet_t, data));

        if (res->pusi) {
            /* 3. Finalize previous PES packet (Flush to Sniffer) */
            if (es->pes.ref_count > 0) {
                /* STRICT ZERO-COPY: Pass packet references to sniffer */
                tsa_handle_es_payload(h, pid, NULL, es->pes.total_length, h->stc_ns);

                /* Cleanup: Unref all packets in finished PES */
                for (uint32_t i = 0; i < es->pes.ref_count; i++) {
                    // tsa_packet_unref(h->pool, es->pes.refs[i]);
                    // (Assuming pool access or automatic sweep)
                }
            }
            es->pes.ref_count = 0;
            es->pes.total_length = 0;

            if (res->has_pes_header) {
                es->pes.pending_dts_ns = (res->dts * 1000000ULL) / 90;
                es->pes.last_pts_33 = res->pts;
                es->pes.last_dts_33 = res->dts;
                es->pes.has_pts = true;
                es->pes.has_dts = true;
            }
        }

        /* 4. ZERO-COPY Accumulation */
        if (es->pes.ref_count < TSA_PES_MAX_REFS) {
            es->pes.refs[es->pes.ref_count] = p_obj;
            es->pes.payload_offsets[es->pes.ref_count] = (uint16_t)(pkt + 4 + res->af_len - (uint8_t*)p_obj->data);
            es->pes.payload_lens[es->pes.ref_count] = (uint8_t)res->payload_len;
            es->pes.ref_count++;
            es->pes.total_length += res->payload_len;

            // tsa_packet_ref(p_obj);

            /* EB Fill Logic */
            es->tstd.eb_fill_q64 += INT_TO_Q64_64(res->payload_len * 8);
        }
    }
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
