#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "tsa_internal.h"

void generate_packet(uint8_t* pkt, uint16_t pid, uint8_t cc, bool transport_error) {
    memset(pkt, 0, 188);
    pkt[0] = 0x47;
    pkt[1] = (pid >> 8) & 0x1F;
    if (transport_error) pkt[1] |= 0x80;
    pkt[2] = pid & 0xFF;
    pkt[3] = 0x10 | (cc & 0x0F); // Payload only, no AF
}

int main() {
    printf("Testing Health & Debounce Model...\n");

    tsa_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.analysis.tr101290 = true;

    tsa_handle_t* h = tsa_create(&cfg);
    assert(h != NULL);

    uint8_t pkt[188];
    uint64_t now = 1000000000ULL; // 1s

    // 1. Initial State - Perfect Health
    h->signal_lock = true;
    tsa_commit_snapshot(h, now);
    printf("Initial Health: %.2f\n", h->last_health_score);
    assert(h->last_health_score > 99.0);

    // 2. Transient CC Error (Single Packet)
    // Send some good packets first to establish state
    for (int i = 0; i < 10; i++) {
        generate_packet(pkt, 0x100, i % 16, false);
        tsa_process_packet(h, pkt, now + (i * 1000000ULL));
    }

    // Trigger one CC error
    generate_packet(pkt, 0x100, 12, false); // Jump from 9 to 12
    tsa_process_packet(h, pkt, now + (10 * 1000000ULL));

    // Check CC alarm - should NOT be fired (debounced 100ms)
    assert(h->debounce_cc.is_fired == false);
    tsa_commit_snapshot(h, now + (11 * 1000000ULL));
    printf("Health after transient CC: %.2f (Alarm: %s)\n",
           h->last_health_score, h->debounce_cc.is_fired ? "FIRED" : "CLEAN");
    assert(h->debounce_cc.is_fired == false);
    assert(h->last_health_score > 95.0); // Should still be high

    // 3. Sustained CC Error (>100ms)
    for (int i = 0; i < 150; i++) { // 150ms of errors
        generate_packet(pkt, 0x100, (i * 2) % 16, false); // Constant CC jumps
        tsa_process_packet(h, pkt, now + (20 * 1000000ULL) + (i * 1000000ULL));
    }

    // Check CC alarm - should be FIRED now
    assert(h->debounce_cc.is_fired == true);
    tsa_commit_snapshot(h, now + (200 * 1000000ULL));
    printf("Health after sustained CC: %.2f (Alarm: %s)\n",
           h->last_health_score, h->debounce_cc.is_fired ? "FIRED" : "CLEAN");
    assert(h->debounce_cc.is_fired == true);
    assert(h->last_health_score < 80.0); // Should have dropped (at least -25)

    // 4. Recovery (Wait M=2000ms)
    uint64_t recovery_start = now + (300 * 1000000ULL);
    for (int i = 0; i < 2100; i++) { // 2.1s of clean traffic
        generate_packet(pkt, 0x100, i % 16, false);
        tsa_process_packet(h, pkt, recovery_start + (i * 1000000ULL));
    }

    // Check CC alarm - should be RESOLVED
    assert(h->debounce_cc.is_fired == false);
    tsa_commit_snapshot(h, recovery_start + (2200 * 1000000ULL));
    printf("Health after recovery: %.2f (Alarm: %s)\n",
           h->last_health_score, h->debounce_cc.is_fired ? "FIRED" : "CLEAN");
    assert(h->debounce_cc.is_fired == false);
    assert(h->last_health_score > 90.0); // Should be recovering (EMA)

    tsa_destroy(h);
    printf("Test PASSED!\n");
    return 0;
}
