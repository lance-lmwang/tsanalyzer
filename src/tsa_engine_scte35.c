#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "tsa_plugin.h"
#include "tsa_internal.h"

typedef struct {
    tsa_handle_t* h;
    uint32_t scte35_count;
    tsa_stream_t stream;
} scte35_engine_t;

static void scte35_on_ts(void* self, const uint8_t* pkt);

static void* scte35_create(void* h) {
    scte35_engine_t* e = calloc(1, sizeof(scte35_engine_t));
    e->h = (tsa_handle_t*)h;
    tsa_stream_init(&e->stream, e, scte35_on_ts);
    return e;
}

static void scte35_destroy(void* engine) {
    scte35_engine_t* ctx = (scte35_engine_t*)engine;
    tsa_stream_destroy(&ctx->stream);
    free(ctx);
}

static tsa_stream_t* scte35_get_stream(void* engine) {
    scte35_engine_t* ctx = (scte35_engine_t*)engine;
    return &ctx->stream;
}

static void scte35_on_ts(void* self, const uint8_t* pkt) {
    (void)pkt;
    scte35_engine_t* e = (scte35_engine_t*)self;
    const ts_decode_result_t* r = &e->h->current_res;

    if (r->pid == 0x1FC || e->h->pid_is_scte35[r->pid]) {
        // Here we'd call the existing tsa_scte35_process,
        // but it needs a gathered section.
        // This is where the modularity shines: the engine can have its own section filter.
    }
}

static void scte35_reset(void* engine) {
    scte35_engine_t* e = (scte35_engine_t*)engine;
    e->scte35_count = 0;
}

tsa_plugin_ops_t tsa_scte35_engine = {
    .name = "SCTE35_PARSER",
    .create = scte35_create,
    .destroy = scte35_destroy,
    .get_stream = scte35_get_stream,
    .reset = scte35_reset,
    .commit = NULL
};
