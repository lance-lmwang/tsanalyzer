#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tsa_internal.h"
#include "tsa_plugin.h"

typedef struct {
    tsa_handle_t* h;
    uint32_t scte35_count;
} scte35_engine_t;

static void scte35_on_ts(void* self, const uint8_t* pkt);

static void* scte35_create(void* h, void* context_buf) {
    scte35_engine_t* e = (scte35_engine_t*)context_buf;
    memset(e, 0, sizeof(scte35_engine_t));
    e->h = (tsa_handle_t*)h;
    return e;
}

static void scte35_destroy(void* engine) {
    (void)engine;
}

static void scte35_on_ts(void* self, const uint8_t* pkt) {
    (void)pkt;
    scte35_engine_t* e = (scte35_engine_t*)self;
    const ts_decode_result_t* r = &e->h->current_res;

    if (r->pid == 0x1FC || e->h->pid_is_scte35[r->pid]) {
        /* modular SCTE-35 handling logic */
    }
}

static void scte35_reset(void* engine) {
    scte35_engine_t* e = (scte35_engine_t*)engine;
    e->scte35_count = 0;
}

tsa_plugin_ops_t tsa_scte35_engine = {.name = "SCTE35_PARSER",
                                      .create = scte35_create,
                                      .destroy = scte35_destroy,
                                      .on_ts = scte35_on_ts,
                                      .reset = scte35_reset,
                                      .commit = NULL};
