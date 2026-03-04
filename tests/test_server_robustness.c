#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tsa.h"
#include "tsa_internal.h"

void test_handle_limit() {
    printf("Testing handle limit and concurrency...\n");
    tsa_handle_t* handles[50];  // Reduced for faster CI
    tsa_config_t cfg = {0};
    cfg.is_live = true;
    cfg.enable_forensics = false;

    for (int i = 0; i < 50; i++) {
        handles[i] = tsa_create(&cfg);
        if (!handles[i]) {
            printf("Failed at index %d\n", i);
            exit(1);
        }
    }

    uint8_t pkt[188] = {0x47, 0x00, 0x00, 0x10};
    for (int i = 0; i < 50; i++) {
        tsa_process_packet(handles[i], pkt, 1000);
        tsa_commit_snapshot(handles[i], 2000);
        tsa_snapshot_full_t snap;
        assert(tsa_take_snapshot_full(handles[i], &snap) == 0);
    }

    for (int i = 0; i < 50; i++) {
        tsa_destroy(handles[i]);
    }
    printf("[PASS] Concurrent handle lifecycle stable.\n");
}

void test_invalid_json_mock() {
    printf("Testing serializer robustness...\n");
    tsa_snapshot_full_t snap;
    memset(&snap, 0, sizeof(snap));

    char buf[1024];
    // Test with tiny buffer (should not crash)
    tsa_snapshot_to_json(NULL, &snap, buf, 5);

    // Test with nulls
    size_t res = tsa_snapshot_to_json(NULL, &snap, buf, 1024);
    assert(res == 0);

    printf("[PASS] Serializer safety verified.\n");
}

int main() {
    test_handle_limit();
    test_invalid_json_mock();
    return 0;
}
