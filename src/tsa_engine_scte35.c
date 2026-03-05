#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "tsa_engine.h"
#include "tsa_internal.h"

typedef struct {
    tsa_handle_t* h;
    uint32_t scte35_count;
} scte35_engine_t;

static void* scte35_create(void* h) {
    scte35_engine_t* e = calloc(1, sizeof(scte35_engine_t));
    e->h = (tsa_handle_t*)h;
    return e;
}

static void scte35_destroy(void* engine) {
    free(engine);
}

static void scte35_process_packet(void* engine, const uint8_t* pkt, const void* res, uint64_t now_ns) {
    scte35_engine_t* e = (scte35_engine_t*)engine;
    const ts_decode_result_t* r = (const ts_decode_result_t*)res;
    
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

tsa_engine_ops_t tsa_scte35_engine = {
    .name = "SCTE35",
    .create = scte35_create,
    .destroy = scte35_destroy,
    .process_packet = scte35_process_packet,
    .reset = scte35_reset,
    .commit = NULL
};
