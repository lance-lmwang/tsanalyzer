#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "tsa.h"
#include "tsa_internal.h"
#include "tsp.h"

void encode_pcr(uint8_t* pkt, uint64_t ticks) {
    uint64_t base = ticks / 300;
    uint16_t ext = (uint16_t)(ticks % 300);
    pkt[6] = (uint8_t)(base >> 25);
    pkt[7] = (uint8_t)(base >> 17);
    pkt[8] = (uint8_t)(base >> 9);
    pkt[9] = (uint8_t)(base >> 1);
    pkt[10] = (uint8_t)((base << 7) | 0x7E | (ext >> 8));
    pkt[11] = (uint8_t)(ext & 0xFF);
}

void test_dejitter_performance() {
    printf("Running test_dejitter_performance...\n");

    tsa_gateway_config_t g_cfg = {0};
    g_cfg.pacing.bitrate = 10000000;  // 10Mbps
    g_cfg.pacing.dest_ip = "127.0.0.1";
    g_cfg.pacing.port = 1234;
    g_cfg.pacing.ts_per_udp = 7;
    g_cfg.enable_action_engine = true;
    g_cfg.enable_pcr_restamp = true;

    tsa_gateway_t* gw = tsa_gateway_create(&g_cfg);
    assert(gw != NULL);

    uint8_t pkt[188];
    memset(pkt, 0, 188);
    pkt[0] = 0x47;
    pkt[1] = 0x01;  // PID 0x100
    pkt[3] = 0x30;  // AF + Payload
    pkt[4] = 7;
    pkt[5] = 0x10;  // PCR

    // We simulate jittery input
    const int count = 500;
    uint64_t pcr_val = 0;
    uint64_t local_now = 1000000000ULL;

    for (int i = 0; i < count; i++) {
        int jitter_ticks = (rand() % 27000) - 13500;  // +/- 0.5ms jitter (Actually 27000 is 1ms)
        // Let's use 0.5ms = 13500 ticks
        encode_pcr(pkt, pcr_val + jitter_ticks);

        tsa_gateway_process(gw, pkt, local_now);

        pcr_val += 1080000;     // 40ms
        local_now += 40000000;  // 40ms local interval
    }

    sleep(1);

    tsa_snapshot_full_t snap;
    tsa_take_snapshot_full(tsa_gateway_get_tsa_handle(gw), &snap);

    printf("Resulting PCR Jitter Max: %ld ns\n", snap.stats.pcr_jitter_max_ns);

    // With re-stamping, jitter should be minimized
    assert(snap.stats.pcr_jitter_max_ns < 100000);

    tsa_gateway_destroy(gw);
    printf("test_dejitter_performance passed.\n");
}

int main() {
    srand(time(NULL));
    test_dejitter_performance();
    return 0;
}
