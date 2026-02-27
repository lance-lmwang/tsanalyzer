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

void test_gateway_fail_safe() {
    printf("Running test_gateway_fail_safe...\n");

    tsa_gateway_config_t g_cfg = {0};
    g_cfg.pacing.bitrate = 100000000;
    g_cfg.pacing.dest_ip = "127.0.0.1";
    g_cfg.pacing.port = 9999;
    g_cfg.pacing.ts_per_udp = 7;
    g_cfg.watchdog_timeout_ns = 5000000ULL;  // 5ms

    tsa_gateway_t* gw = tsa_gateway_create(&g_cfg);
    assert(gw != NULL);
    assert(tsa_gateway_is_bypassing(gw) == false);

    uint8_t pkt[188];
    memset(pkt, 0, 188);
    pkt[0] = 0x47;

    // Normal packet
    tsa_gateway_process(gw, pkt, 1000000ULL);
    assert(tsa_gateway_is_bypassing(gw) == false);

    // Inject stall (10ms > 5ms watchdog)
    printf("Injecting 10ms stall to trigger watchdog...\n");
    tsa_gateway_debug_inject_stall(gw, 10000000ULL);
    tsa_gateway_process(gw, pkt, 2000000ULL);

    assert(tsa_gateway_is_bypassing(gw) == true);
    printf("Watchdog triggered bypass successfully.\n");

    tsa_gateway_destroy(gw);
}

int main() {
    test_gateway_fail_safe();
    return 0;
}
