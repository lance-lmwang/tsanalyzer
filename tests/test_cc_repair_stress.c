#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "tsa.h"
#include "tsa_internal.h"
#include "tsp.h"

void test_cc_repair_stress() {
    printf("Running test_cc_repair_stress (10%% loss simulation)...\n");

    tsa_gateway_config_t g_cfg = {0};
    // Use high bitrate to ensure we don't hit pacer limits
    g_cfg.pacing.bitrate = 100000000;  // 100 Mbps
    g_cfg.pacing.dest_ip = "127.0.0.1";
    g_cfg.pacing.port = 1236;
    g_cfg.pacing.ts_per_udp = 7;
    g_cfg.enable_action_engine = true;
    g_cfg.enable_null_substitution = true;

    tsa_gateway_t* gw = tsa_gateway_create(&g_cfg);
    assert(gw != NULL);

    srand(time(NULL));

    uint8_t pkt[188];
    memset(pkt, 0, 188);
    pkt[0] = 0x47;
    pkt[1] = 0x01;  // PID 0x100

    int total_to_send = 10000;
    int sent_count = 0;
    int dropped_count = 0;
    uint8_t cc = 0;

    uint64_t now_ns = 1000000000ULL;
    uint64_t step_ns = 150400;  // ~100 Mbps (188*8*1e9/100e6)

    for (int i = 0; i < total_to_send; i++) {
        if (rand() % 100 < 10) {
            // Drop
            dropped_count++;
        } else {
            pkt[3] = 0x10 | cc;
            tsa_gateway_process(gw, pkt, now_ns);
            sent_count++;
        }
        cc = (cc + 1) & 0x0F;
        now_ns += step_ns;
    }

    printf("Sent: %d, Dropped: %d\n", sent_count, dropped_count);

    tsa_commit_snapshot(tsa_gateway_get_tsa_handle(gw), now_ns + 1000000);

    tsa_snapshot_full_t snap;
    tsa_take_snapshot_full(tsa_gateway_get_tsa_handle(gw), &snap);

    printf("TSA Detected CC Loss: %lu\n", snap.stats.cc_loss_count);

    // TSA should detect approximately the dropped count.
    // Since we increment CC every time even for dropped packets, it should be exact.
    // However, if we drop multiple packets in a row, TSA counts them correctly.
    assert(snap.stats.cc_loss_count >= (uint64_t)dropped_count - 1);

    tsa_gateway_destroy(gw);
    printf("test_cc_repair_stress passed.\n");
}

int main() {
    test_cc_repair_stress();
    return 0;
}
