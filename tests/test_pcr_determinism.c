#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "tsa_clock.h"

/* Helper to create a dummy TS packet with PCR */
void make_pcr_packet(uint8_t* buf, uint64_t pcr_ticks, bool discontinuity) {
    memset(buf, 0, 188);
    buf[0] = 0x47;
    buf[3] = 0x20; /* AF present */
    buf[4] = 7;    /* AF length */
    buf[5] = 0x10; /* PCR flag */
    if (discontinuity) buf[5] |= 0x80;

    uint64_t base = pcr_ticks / 300;
    uint16_t ext = pcr_ticks % 300;

    buf[6] = (base >> 25) & 0xFF;
    buf[7] = (base >> 17) & 0xFF;
    buf[8] = (base >> 9) & 0xFF;
    buf[9] = (base >> 1) & 0xFF;
    buf[10] = ((base & 0x01) << 7) | 0x7E | ((ext >> 8) & 0x01);
    buf[11] = ext & 0xFF;
}

int main() {
    tsa_clock_inspector_t inspector;
    tsa_clock_reset(&inspector);
    inspector.pid = 0x100;

    uint8_t pkt[188];
    uint64_t now_ns = 1000000000ULL;
    uint64_t pcr_val = 27000000ULL; /* 1 second in ticks */

    printf(">>> Testing PCR Determinism Logic (V9)...\n");

    /* 1. Warm up: Establish Lock (requires 10 packets in V9) */
    for (int i = 0; i < 10; i++) {
        make_pcr_packet(pkt, pcr_val, false);
        tsa_clock_update(pkt, &inspector, now_ns, true);
        pcr_val += 540000;     /* 20ms gap */
        now_ns += 20000000ULL; /* 20ms wall clock */
    }
    assert(inspector.state == TSA_CLOCK_STATE_LOCKED);
    assert(inspector.priority_1_errors == 0);
    printf("[PASS] Initialization: Locked after 10 packets with 0 errors.\n");

    /* 2. Scenario: Single Valid Gap (200ms) */
    pcr_val += 5400000;     /* 200ms jump */
    now_ns += 200000000ULL; /* 200ms wall clock */
    make_pcr_packet(pkt, pcr_val, false);
    tsa_clock_update(pkt, &inspector, now_ns, true);

    assert(inspector.priority_1_errors == 1);
    printf("[PASS] Single Gap: Exactly 1 error reported.\n");

    /* 3. Scenario: Micro-Burst (Recovery Explosion)
     * We send packets with large PCR jumps but arriving at < 100us intervals.
     * These should be ignored by the Burst Protection logic. */
    for (int i = 0; i < 50; i++) {
        pcr_val += 1080000; /* 40ms+ jump */
        now_ns += 50000ULL; /* ONLY 50us wall clock (Burst!) */
        make_pcr_packet(pkt, pcr_val, false);
        tsa_clock_update(pkt, &inspector, now_ns, true);
    }
    assert(inspector.priority_1_errors == 1);
    printf("[PASS] Micro-Burst: Still exactly 1 error (Burst ignored as expected).\n");

    /* 4. Scenario: Loop-back (Backward Jump)
     * Resetting PCR to a small value. Should trigger RESET and stay silent. */
    pcr_val = 1000000ULL;
    now_ns += 20000000ULL;
    make_pcr_packet(pkt, pcr_val, false);
    tsa_clock_update(pkt, &inspector, now_ns, true);

    assert(inspector.priority_1_errors == 1);
    assert(inspector.state == TSA_CLOCK_STATE_SYNCING);
    printf("[PASS] Loop-back: Correctly reset state machine without false error.\n");

    printf("\n>>> ALL PCR DETERMINISM TESTS PASSED <<<\n");
    return 0;
}
