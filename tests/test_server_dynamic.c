#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "tsa.h"
#include "tsa_internal.h"

// Mocking backend core logic test
void test_dynamic_injection_logic() {
    printf("=== RUNNING DYNAMIC INJECTION LOGIC TEST ===\n");

    // 1. Create configuration
    tsa_config_t cfg = {.op_mode = TSA_MODE_LIVE};
    snprintf(cfg.input_label, sizeof(cfg.input_label), "TEST-1");

    // 2. Create instance
    tsa_handle_t* h = tsa_create(&cfg);
    assert(h != NULL);
    printf("[PASS] Instance created for TEST-1\n");

    // 3. Simulate packet injection
    uint8_t dummy_pkt[188] = {0x47, 0x00, 0x00, 0x10};  // PAT Start
    uint64_t now = 1000000;

    tsa_process_packet(h, dummy_pkt, now);
    printf("[PASS] Packet processed\n");

    // 4. Verify metrics export
    tsa_snapshot_lite_t snap;
    int r = tsa_take_snapshot_lite(h, &snap);
    assert(r == 0);
    assert(snap.master_health > 0);
    printf("[PASS] Metrics valid: health=%.1f\n", snap.master_health);

    tsa_destroy(h);
    printf("=== ALL DYNAMIC LOGIC TESTS PASSED ===\n");
}

int main() {
    test_dynamic_injection_logic();
    return 0;
}
