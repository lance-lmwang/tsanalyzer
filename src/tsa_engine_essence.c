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

static void* essence_create(void* h) {
    essence_ctx_t* ctx = calloc(1, sizeof(essence_ctx_t));
    ctx->h = (tsa_handle_t*)h;
    tsa_stream_init(&ctx->stream, ctx, essence_on_ts);
    return ctx;
}

static void essence_destroy(void* engine) {
    essence_ctx_t* ctx = (essence_ctx_t*)engine;
    tsa_stream_destroy(&ctx->stream);
    free(ctx);
}

static tsa_stream_t* essence_get_stream(void* engine) {
    essence_ctx_t* ctx = (essence_ctx_t*)engine;
    return &ctx->stream;
}

static void essence_on_ts(void* self, const uint8_t* pkt) {
    essence_ctx_t* ctx = (essence_ctx_t*)self;
    tsa_handle_t* h = ctx->h;
    const ts_decode_result_t* res = &h->current_res;
    uint64_t now = h->current_ns;
    (void)now;
    uint16_t pid = res->pid;

    if (res->payload_len > 0) {
        if (res->pusi) {
            if (h->live->pid_is_referenced[pid] && h->pid_pes_len[pid] > 0) {
                uint8_t tail = h->pid_au_tail[pid];
                uint8_t next_tail = (tail + 1) % 32;
                if (next_tail != h->pid_au_head[pid]) {
                    h->pid_au_q[pid][tail].dts_ns = h->pid_pending_dts[pid];
                    h->pid_au_q[pid][tail].size = h->pid_pes_len[pid];
                    h->pid_au_tail[pid] = next_tail;
                }
                tsa_handle_es_payload(h, pid, h->pid_pes_buf[pid], h->pid_pes_len[pid], h->stc_ns);
            }
            h->pid_pes_len[pid] = 0;

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

            if (h->pid_pes_buf[pid] == NULL && h->pes_pool_used < 32) {
                h->pid_pes_buf[pid] = tsa_mem_pool_alloc(h, 65536);
                h->pid_pes_cap[pid] = 65536;
                h->pes_pool_used++;
            }
        }

        if (h->pid_pes_buf[pid] && h->pid_pes_len[pid] + res->payload_len <= h->pid_pes_cap[pid]) {
            memcpy(h->pid_pes_buf[pid] + h->pid_pes_len[pid], pkt + 4 + res->af_len, res->payload_len);
            h->pid_pes_len[pid] += res->payload_len;

            h->pid_eb_fill_q64[pid] += INT_TO_Q64_64(res->payload_len);
            if (h->pid_eb_fill_q64[pid] > INT_TO_Q64_64(1200000)) { // T-STD standard capacity fallback
                tsa_push_event(h, TSA_EVENT_TSTD_OVERFLOW, pid, 0);
            }
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
    extern void tsa_plugin_attach_instance(tsa_handle_t* h, tsa_plugin_ops_t* ops);
    tsa_plugin_attach_instance(h, &essence_ops);
}