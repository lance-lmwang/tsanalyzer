#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tsa.h"
#include "tsa_internal.h"

void test_pid_lookup_basic() {
    printf("Running test_pid_lookup_basic...\n");
    tsa_config_t cfg = {0};
    tsa_handle_t* h = tsa_create(&cfg);
    assert(h != NULL);

    // Process a packet with PID 0x100 (256)
    uint8_t pkt[188] = {TS_SYNC_BYTE, 0x01, 0x00, 0x10};
    tsa_process_packet(h, pkt, 1000);

    tsa_snapshot_lite_t snap;
    tsa_take_snapshot_lite(h, &snap);

    printf("Active PID count: %u\n", snap.active_pid_count);
    assert(snap.active_pid_count == 1);

    tsa_destroy(h);
    printf("test_pid_lookup_basic passed.\n");
}

void test_pid_eviction_lru() {
    printf("Running test_pid_eviction_lru...\n");
    tsa_config_t cfg = {0};
    tsa_handle_t* h = tsa_create(&cfg);
    assert(h != NULL);

    // Fill the tracker (limit 128) with PIDs 100..227
    // These are all evictable
    for (int i = 0; i < 128; i++) {
        uint16_t pid = i + 100;
        uint8_t pkt[188] = {TS_SYNC_BYTE, (uint8_t)(pid >> 8), (uint8_t)(pid & 0xFF), 0x10};
        tsa_process_packet(h, pkt, 1000 + i);
    }

    tsa_snapshot_lite_t snap;
    tsa_take_snapshot_lite(h, &snap);
    printf("Active PID count after 128 PIDs: %u\n", snap.active_pid_count);
    assert(snap.active_pid_count == 128);

    // Add one more PID, should trigger eviction of PID 100 (LRU)
    uint16_t new_pid = 500;
    uint8_t pkt[188] = {TS_SYNC_BYTE, (uint8_t)(new_pid >> 8), (uint8_t)(new_pid & 0xFF), 0x10};
    tsa_process_packet(h, pkt, 2000);

    tsa_take_snapshot_lite(h, &snap);
    printf("Active PID count after eviction: %u\n", snap.active_pid_count);
    assert(snap.active_pid_count == 128);

    tsa_snapshot_full_t full;
    tsa_take_snapshot_full(h, &full);

    bool found_500 = false;
    bool found_100 = false;
    for (int i = 0; i < 8192; i++) {
        if (full.pids[i].pid == 500 && full.pids[i].liveness_status == 1) found_500 = true;
        if (full.pids[i].pid == 100 && full.pids[i].liveness_status == 1) found_100 = true;
    }

    printf("Found new PID 500: %s, Found evicted PID 100: %s\n", found_500 ? "Yes" : "No", found_100 ? "Yes" : "No");

    assert(found_500);
    assert(!found_100);

    tsa_destroy(h);
    printf("test_pid_eviction_lru passed.\n");
}

int main() {
    test_pid_lookup_basic();
    test_pid_eviction_lru();
    return 0;
}
