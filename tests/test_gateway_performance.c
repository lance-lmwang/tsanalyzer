#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "tsa.h"
#include "tsa_internal.h"
#include "tsp.h"

// Test if we can relay packets with minimal processing overhead
void test_gateway_performance() {
    printf("Running test_gateway_performance...\n");

    tsa_gateway_config_t g_cfg = {0};
    g_cfg.pacing.bitrate = 100000000;
    g_cfg.pacing.dest_ip = "127.0.0.1";
    g_cfg.pacing.port = 9999;
    g_cfg.pacing.ts_per_udp = 7;

    tsa_gateway_t* gw = tsa_gateway_create(&g_cfg);
    assert(gw != NULL);

    uint8_t pkt[188];
    memset(pkt, 0, 188);
    pkt[0] = 0x47;

    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);

    const int count = 1000000;
    for (int i = 0; i < count; i++) {
        uint64_t now = (uint64_t)i * 1000;
        tsa_gateway_process(gw, pkt, now);
    }

    clock_gettime(CLOCK_MONOTONIC, &end);
    double elapsed = (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) / 1e9;
    printf("Gateway processed %d packets in %.3f seconds (%.2f MPPS)\n", count, elapsed, (count / elapsed) / 1e6);

    tsa_gateway_destroy(gw);
}

int main() {
    test_gateway_performance();
    return 0;
}
