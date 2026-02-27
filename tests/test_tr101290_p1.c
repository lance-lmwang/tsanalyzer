#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "tsa.h"
#include "tsa_internal.h"

void test_p1_sync_byte() {
    printf("Running test_p1_sync_byte...\n");
    tsa_config_t cfg = {0};
    tsa_handle_t* h = tsa_create(&cfg);
    assert(h != NULL);

    uint8_t pkt[188];
    memset(pkt, 0, 188);
    pkt[0] = 0x47;  // Valid sync byte

    tsa_process_packet(h, pkt, 1000000000ULL);

    tsa_snapshot_full_t s;
    tsa_take_snapshot_full(h, &s);
    assert(s.stats.sync_byte_error_count == 0);

    // Corrupt sync byte
    pkt[0] = 0x00;
    tsa_process_packet(h, pkt, 1000001000ULL);

    tsa_commit_snapshot(h, 1000002000ULL);
    tsa_take_snapshot_full(h, &s);
    assert(s.stats.sync_byte_error_count == 1);

    tsa_destroy(h);
    printf("test_p1_sync_byte passed.\n");
}

void test_p1_pat_timeout() {
    printf("Running test_p1_pat_timeout...\n");
    tsa_config_t cfg = {0};
    tsa_handle_t* h = tsa_create(&cfg);
    assert(h != NULL);

    uint8_t pkt[188];
    memset(pkt, 0, 188);
    pkt[0] = 0x47;
    // Set PID to non-PAT
    pkt[1] = 0x1F;
    pkt[2] = 0xFF;

    // Process packets for 600ms (PAT timeout is usually 500ms)
    for (int i = 0; i < 100; i++) {
        tsa_process_packet(h, pkt, 1000000000ULL + (uint64_t)i * 10000000ULL);
    }

    tsa_commit_snapshot(h, 2000000000ULL);
    tsa_snapshot_full_t s;
    tsa_take_snapshot_full(h, &s);

    // Should have PAT error
    assert(s.stats.pat_error_count > 0);

    tsa_destroy(h);
    printf("test_p1_pat_timeout passed.\n");
}

int main() {
    test_p1_sync_byte();
    test_p1_pat_timeout();
    return 0;
}
