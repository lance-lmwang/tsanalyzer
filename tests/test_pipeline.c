#include <assert.h>
#include <stdio.h>

#include "tsa_log.h"
#include "tsa_pipeline.h"

static int g_metrology_calls = 0;
static int g_repair_calls = 0;

static void metrology_tap(tsa_stage_t* stage, tsa_packet_t* pkt) {
    (void)stage;
    (void)pkt;
    g_metrology_calls++;
}

static void cc_repair_stage(tsa_stage_t* stage, tsa_packet_t* pkt) {
    (void)stage;
    // Simulate mutating the packet
    pkt->data[0] = 0x47;
    g_repair_calls++;
}

int main() {
    tsa_log_set_level(TSA_LOG_DEBUG);
    tsa_info("pipeline_test", "Starting pipeline graph tests...");

    tsa_pipeline_t pipeline;
    tsa_pipeline_init(&pipeline);

    tsa_stage_t s1 = {.name = "metrology", .is_mutating = false, .process = metrology_tap};
    tsa_stage_t s2 = {.name = "cc_repair", .is_mutating = true, .process = cc_repair_stage};

    tsa_pipeline_add_stage(&pipeline, &s1);
    tsa_pipeline_add_stage(&pipeline, &s2);

    tsa_packet_t pkt = {0};
    tsa_pipeline_dispatch(&pipeline, &pkt);

    assert(g_metrology_calls == 1);
    assert(g_repair_calls == 1);
    assert(pkt.data[0] == 0x47);

    tsa_info("pipeline_test", "All pipeline tests PASSED.");
    return 0;
}
