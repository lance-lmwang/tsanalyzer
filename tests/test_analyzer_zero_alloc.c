#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tsa.h"
#include "tsa_internal.h"

// Mock TS packet with varying CC
static void create_ts_packet(uint8_t* pkt, uint16_t pid, uint8_t cc) {
    memset(pkt, 0, 188);
    pkt[0] = 0x47;
    pkt[1] = (pid >> 8) & 0x1F;
    pkt[2] = pid & 0xFF;
    pkt[3] = 0x10 | (cc & 0x0F);  // Payload only
}

int main() {
    printf("Starting test_analyzer_zero_alloc...\n");

    tsa_config_t cfg = {0};
    tsa_handle_t* h = tsa_create(&cfg);
    assert(h != NULL);

    uint8_t pkt[188];
    uint64_t ts = 1000000;

    // Send 10 packets sequentially to establish state
    for (int i = 0; i < 10; i++) {
        create_ts_packet(pkt, 0x100, i & 0x0F);
        tsa_process_packet(h, pkt, ts);
        ts += 100000;
    }

    tsa_commit_snapshot(h, ts);
    uint64_t cc_errors_before = h->live->cc_error.count;

    // Inject a discontinuity (CC error)
    create_ts_packet(pkt, 0x100, 15);  // Gap!
    tsa_process_packet(h, pkt, ts);
    ts += 100000;

    // Send normal packet
    create_ts_packet(pkt, 0x100, 0);
    tsa_process_packet(h, pkt, ts);
    ts += 100000;

    tsa_commit_snapshot(h, ts);
    uint64_t cc_errors_after_gap = h->live->cc_error.count;

    assert(cc_errors_after_gap > cc_errors_before);
    printf("Successfully detected standard CC error. Count: %llu\n", (unsigned long long)cc_errors_after_gap);

    // Now, trigger an internal drop
    tsa_handle_internal_drop(h, 100);

    // Inject another discontinuity right after internal drop
    create_ts_packet(pkt, 0x100, 5);  // Massive Gap!
    tsa_process_packet(h, pkt, ts);
    ts += 100000;

    // Resume normally
    create_ts_packet(pkt, 0x100, 6);
    tsa_process_packet(h, pkt, ts);
    ts += 100000;

    tsa_commit_snapshot(h, ts);
    uint64_t cc_errors_after_drop = h->live->cc_error.count;

    // CC errors should NOT have increased because of the drop resync
    assert(cc_errors_after_drop == cc_errors_after_gap);
    printf("Successfully ignored CC error after internal drop resync!\n");
    assert(h->live->internal_analyzer_drop == 100);

    tsa_destroy(h);
    printf("test_analyzer_zero_alloc PASS\n");
    return 0;
}
