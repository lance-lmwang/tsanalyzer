#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tsa.h"
#include "tsa_internal.h"

void test_pid_protection() {
    printf("Running test_pid_protection...\n");
    tsa_config_t cfg = {0};
    // Mark PID 1 as protected
    cfg.protected_pids[0] = 1;
    cfg.protected_pids[1] = 0;  // Terminate list

    tsa_handle_t* h = tsa_create(&cfg);
    assert(h != NULL);

    // 1. Process PID 1 (it becomes head)
    uint8_t pkt1[188] = {TS_SYNC_BYTE, 0x00, 0x01, 0x10};
    tsa_process_packet(h, pkt1, 1000);

    // 2. Fill the remaining slots with unique PIDs
    for (int i = 2; i <= MAX_ACTIVE_PIDS; i++) {
        uint16_t pid = i;
        uint8_t pkt[188] = {TS_SYNC_BYTE, (uint8_t)(pid >> 8), (uint8_t)(pid & 0xFF), 0x10};
        tsa_process_packet(h, pkt, 1000 + i);
    }

    // Total count should be MAX_ACTIVE_PIDS
    tsa_snapshot_lite_t snap;
    tsa_take_snapshot_lite(h, &snap);
    assert(snap.active_pid_count == MAX_ACTIVE_PIDS);

    // 3. Add one more PID (PID 4000)
    // It should NOT evict PID 1. It should evict PID 2 (the next LRU)
    uint8_t pkt200[188] = {TS_SYNC_BYTE, 0x0F, 0xA0, 0x10};
    tsa_process_packet(h, pkt200, 10000);

    tsa_commit_snapshot(h, 10000);

    tsa_snapshot_full_t full;
    tsa_take_snapshot_full(h, &full);

    bool found_1 = false;
    bool found_2 = false;
    bool found_4000 = false;
    for (uint32_t i = 0; i < full.active_pid_count; i++) {
        if (full.pids[i].pid == 1 && full.pids[i].liveness_status == 1) found_1 = true;
        if (full.pids[i].pid == 2 && full.pids[i].liveness_status == 1) found_2 = true;
        if (full.pids[i].pid == 4000 && full.pids[i].liveness_status == 1) found_4000 = true;
    }

    printf("Found Protected PID 1: %s (Expected: Yes)\n", found_1 ? "Yes" : "No");
    printf("Found Evictable PID 2: %s (Expected: No)\n", found_2 ? "Yes" : "No");
    printf("Found New PID 4000: %s (Expected: Yes)\n", found_4000 ? "Yes" : "No");

    // In Red Phase, these assertions will likely fail
    assert(found_1);
    assert(!found_2);
    assert(found_4000);

    tsa_destroy(h);
    printf("test_pid_protection passed.\n");
}

int main() {
    test_pid_protection();
    return 0;
}
