#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "tsa.h"
#include "tsa_internal.h"

void test_gateway_handoff_latency() {
    printf("Running test_gateway_handoff_latency...\n");

    tsa_gateway_config_t g_cfg = {0};
    g_cfg.pacing.bitrate = 100000000;
    g_cfg.pacing.dest_ip = "127.0.0.1";
    g_cfg.pacing.port = 9999;
    g_cfg.pacing.ts_per_udp = 7;
    g_cfg.enable_action_engine = true;

    tsa_gateway_t* gw = tsa_gateway_create(&g_cfg);
    assert(gw != NULL);

    uint8_t pkt[188];
    memset(pkt, 0, 188);
    pkt[0] = 0x47;

    struct timespec start, end;
    
    // Warmup
    for(int i=0; i<100; i++) tsa_gateway_process(gw, pkt, i*1000);

    clock_gettime(CLOCK_MONOTONIC, &start);
    tsa_gateway_process(gw, pkt, 100*1000);
    clock_gettime(CLOCK_MONOTONIC, &end);

    uint64_t latency_ns = (end.tv_sec - start.tv_sec) * 1000000000ULL + (end.tv_nsec - start.tv_nsec);
    printf("Single packet handoff latency: %lu ns\n", latency_ns);

    // Watchdog limit is 5ms (5,000,000 ns)
    assert(latency_ns < 5000000ULL);

    tsa_gateway_destroy(gw);
}

int main() {
    test_gateway_handoff_latency();
    return 0;
}
