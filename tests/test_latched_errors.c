#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tsa.h"

void test_latched_errors() {
    printf("Testing Latched Errors (Smart Latching)...\n");
    
    tsa_config_t cfg = {0};
    cfg.is_live = true;
    tsa_handle_t* h = tsa_create(&cfg);
    assert(h != NULL);

    // 1. Initial state (all zero)
    tsa_snapshot_full_t snap;
    tsa_take_snapshot_full(h, &snap);
    assert(snap.stats.latched_cc_error == 0);

    // 2. Inject a CC error
    uint8_t pkt[188] = {0x47, 0x01, 0x00, 0x11}; // CC=1
    tsa_process_packet(h, pkt, 1000);
    pkt[3] = 0x13; // CC=3 (jumped from 1, CC=2 missing)
    tsa_process_packet(h, pkt, 2000);

    tsa_commit_snapshot(h, 3000);
    tsa_take_snapshot_full(h, &snap);
    assert(snap.stats.cc_error_count > 0);
    assert(snap.stats.latched_cc_error == 1);

    // 3. Inject GOOD packets (latched error should stay 1)
    pkt[3] = 0x14; tsa_process_packet(h, pkt, 4000);
    pkt[3] = 0x15; tsa_process_packet(h, pkt, 5000);
    tsa_commit_snapshot(h, 6000);
    tsa_take_snapshot_full(h, &snap);
    assert(snap.stats.latched_cc_error == 1);

    // 4. Reset latched errors
    tsa_reset_latched_errors(h);
    tsa_commit_snapshot(h, 7000);
    tsa_take_snapshot_full(h, &snap);
    assert(snap.stats.latched_cc_error == 0);

    tsa_destroy(h);
    printf("[PASS] Latched errors verified.\n");
}

int main() {
    test_latched_errors();
    return 0;
}
