#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "tsa.h"
#include "tsa_internal.h"
#include "tsp.h"

void test_cc_repair() {
    printf("Running test_cc_repair...\n");

    tsa_gateway_config_t g_cfg = {0};
    g_cfg.pacing.bitrate = 10000000;
    g_cfg.pacing.dest_ip = "127.0.0.1";
    g_cfg.pacing.port = 1235;
    g_cfg.pacing.ts_per_udp = 1;
    g_cfg.enable_action_engine = true;
    g_cfg.enable_null_substitution = true;

    tsa_gateway_t* gw = tsa_gateway_create(&g_cfg);
    assert(gw != NULL);

    uint8_t pkt[188];
    memset(pkt, 0, 188);
    pkt[0] = 0x47;
    pkt[1] = 0x01;  // PID 0x100
    pkt[3] = 0x10;  // CC 0

    // 1. Send CC 0
    tsa_gateway_process(gw, pkt, 1000000000ULL);

    // 2. Simulate Loss: Jump CC from 0 to 5
    pkt[3] = 0x15;  // CC 5
    int res = tsa_gateway_process(gw, pkt, 1040000000ULL);
    printf("Process result (lost count): %d\n", res);

    tsa_commit_snapshot(tsa_gateway_get_tsa_handle(gw), 1100000000ULL);

    // Wait for pacer
    sleep(1);

    tsa_snapshot_full_t snap;
    tsa_take_snapshot_full(tsa_gateway_get_tsa_handle(gw), &snap);

    printf("Detected CC Errors: %lu, CC Loss: %lu\n", snap.stats.cc_error_count, snap.stats.cc_loss_count);

    // We expect 4 lost packets (1, 2, 3, 4)
    assert(snap.stats.cc_loss_count == 4);

    uint64_t total_packets = tsp_get_total_packets(tsa_gateway_get_tsp_handle(gw));
    printf("Total Egress Packets: %lu\n", total_packets);

    // We expect at least 6 packets (1st + 4 nulls + 2nd)
    // but CBR pacer will continue stuffing after those.
    assert(total_packets >= 6);

    tsa_gateway_destroy(gw);
    printf("test_cc_repair passed.\n");
}

int main() {
    test_cc_repair();
    return 0;
}
