#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tsa.h"
#include "tsa_internal.h"

void test_snapshot_atomic_logic() {
    printf("Running test_snapshot_atomic_logic...\n");
    tsa_config_t cfg = {0};
    tsa_handle_t* h = tsa_create(&cfg);
    assert(h != NULL);

    tsa_snapshot_lite_t s1, s2;
    tsa_take_snapshot_lite(h, &s1);

    // Simulate some work
    uint8_t pkt[188] = {TS_SYNC_BYTE, 0x01, 0x00, 0x10};
    tsa_process_packet(h, pkt, 2000000000ULL);

    tsa_take_snapshot_lite(h, &s2);

    assert(s2.total_packets >= s1.total_packets);

    tsa_destroy(h);
    printf("test_snapshot_atomic_logic passed.\n");
}

int main() {
    test_snapshot_atomic_logic();
    return 0;
}
