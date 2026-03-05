#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tsa_internal.h"
#include "tsa_plugin.h"

// We will port the H.264 / H.265 detailed SPS/PPS parsing here
// Currently relying on tsa_es.c for basic extraction, but we will make it a subscriber engine.

typedef struct {
    tsa_handle_t* h;
    tsa_stream_t stream;
} codec_ctx_t;

static void codec_on_ts(void* self, const uint8_t* pkt);

static void* codec_create(void* h) {
    codec_ctx_t* ctx = calloc(1, sizeof(codec_ctx_t));
    ctx->h = (tsa_handle_t*)h;
    tsa_stream_init(&ctx->stream, ctx, codec_on_ts);
    return ctx;
}

static void codec_destroy(void* engine) {
    codec_ctx_t* ctx = (codec_ctx_t*)engine;
    tsa_stream_destroy(&ctx->stream);
    free(ctx);
}

static tsa_stream_t* codec_get_stream(void* engine) {
    codec_ctx_t* ctx = (codec_ctx_t*)engine;
    return &ctx->stream;
}

static void codec_on_ts(void* self, const uint8_t* pkt) {
    codec_ctx_t* ctx = (codec_ctx_t*)self;
    tsa_handle_t* h = ctx->h;
    const ts_decode_result_t* res = &h->current_res;
    uint64_t now = h->current_ns;
    uint16_t pid = res->pid;

    if (res->payload_len > 0) {
        if (res->pusi) {
            /* 1. Complete previous PES packet and perform Codec Metadata Extraction */
            if (h->live->pid_is_referenced[pid] && h->pid_pes_len[pid] > 0) {
                // Buffer management for T-STD
                uint8_t tail = h->pid_au_tail[pid];
                uint8_t next_tail = (tail + 1) % 32;
                if (next_tail != h->pid_au_head[pid]) {
                    h->pid_au_q[pid][tail].dts_ns = h->pid_pending_dts[pid];
                    h->pid_au_q[pid][tail].size = h->pid_pes_len[pid];
                    h->pid_au_tail[pid] = next_tail;
                }
                
                // Deep ES Analysis (NAL, SPS, etc.)
                // This function currently lives in tsa_es.c. In a full refactor,
                // the golomb logic and SPS parsing would be moved into this file.
                tsa_handle_es_payload(h, pid, h->pid_pes_buf[pid], h->pid_pes_len[pid], h->stc_ns);
            }
            h->pid_pes_len[pid] = 0;

            /* 2. Process new PES Header for A/V Sync */
            if (res->has_pes_header) {
                h->pid_pending_dts[pid] = (res->dts * 1000000ULL) / 90;

                const char* st = tsa_get_pid_type_name(h, pid);
                bool is_video = (strcmp(st, "H.264") == 0 || strcmp(st, "HEVC") == 0 || strcmp(st, "MPEG2-V") == 0);
                bool is_audio = (strcmp(st, "AAC") == 0 || strcmp(st, "ADTS-AAC") == 0 || 
                                 strcmp(st, "MPEG1-A") == 0 || strcmp(st, "MPEG2-A") == 0 || strcmp(st, "AC3") == 0);

                if (is_video) {
                    h->last_v_pts = res->pts;
                    if (h->last_a_pts > 0)
                        h->live->av_sync_ms = (int32_t)((int64_t)h->last_v_pts - (int64_t)h->last_a_pts) / 90;
                } else if (is_audio) {
                    h->last_a_pts = res->pts;
                    if (h->last_v_pts > 0)
                        h->live->av_sync_ms = (int32_t)((int64_t)h->last_v_pts - (int64_t)h->last_a_pts) / 90;
                }
            }
            
            /* If no buffer assigned yet, grab one from the pool */
            if (h->pid_pes_buf[pid] == NULL && h->pes_pool_used < 32) {
                h->pid_pes_buf[pid] = tsa_mem_pool_alloc(h, 65536);
                h->pid_pes_cap[pid] = 65536;
                h->pes_pool_used++;
            }
        }

        /* 3. Reassemble PES payload */
        if (h->pid_pes_buf[pid] && h->pid_pes_len[pid] + res->payload_len <= h->pid_pes_cap[pid]) {
            memcpy(h->pid_pes_buf[pid] + h->pid_pes_len[pid], pkt + 4 + res->af_len, res->payload_len);
            h->pid_pes_len[pid] += res->payload_len;
        }
    }
}

static tsa_plugin_ops_t codec_ops = {
    .name = "CODEC_METADATA",
    .create = codec_create,
    .destroy = codec_destroy,
    .get_stream = codec_get_stream,
};

void tsa_register_codec_engine(tsa_handle_t* h) {
    extern void tsa_plugin_attach_instance(tsa_handle_t* h, tsa_plugin_ops_t* ops);
    tsa_plugin_attach_instance(h, &codec_ops);
}