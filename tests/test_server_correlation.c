#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tsa.h"
#include "tsa_internal.h"

void test_global_correlation() {
    printf("Running test_global_correlation...\n");
    tsa_handle_t* h[4];
    tsa_config_t cfg = {0};
    uint8_t pkt[188] = {0x47, 0x00, 0x00, 0x10};

    for (int i = 0; i < 4; i++) {
        sprintf(cfg.input_label, "STR-%d", i);
        h[i] = tsa_create(&cfg);
        // Make them "active"
        tsa_process_packet(h[i], pkt, 1000000);
        tsa_commit_snapshot(h[i], 2000000);
    }

    char buf[65536];

    // 1. All healthy (4 active streams)
    tsa_exporter_prom_v2(h, 4, buf, sizeof(buf));
    assert(strstr(buf, "tsa_global_network_incident 0") != NULL);

    // 2. 3 out of 4 fail
    for (int i = 0; i < 3; i++) {
        h[i]->live.sync_loss.count++;
        tsa_commit_snapshot(h[i], 1000000000ULL);
    }
    tsa_commit_snapshot(h[3], 1000000000ULL);

    tsa_exporter_prom_v2(h, 4, buf, sizeof(buf));
    assert(strstr(buf, "tsa_global_network_incident 1") != NULL);

    for (int i = 0; i < 4; i++) tsa_destroy(h[i]);
    printf("test_global_correlation passed.\n");
}

int main() {
    test_global_correlation();
    return 0;
}
