#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "tsa_clock.h"

/* Helper to create a dummy TS packet with PCR */
void make_pcr_packet(uint8_t* buf, uint64_t pcr_ticks) {
    memset(buf, 0, 188);
    buf[0] = 0x47;
    buf[3] = 0x20; /* AF present */
    buf[4] = 7;    /* AF length */
    buf[5] = 0x10; /* PCR flag */

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
    tsa_clock_inspector_t inspector = {0};
    tsa_clock_reset(&inspector);
    inspector.pid = 0x100;

    uint8_t pkt[188];
    uint64_t now_ns = 1000000000ULL;
    uint64_t pcr_val = 27000000ULL;

    printf(">>> Testing PCR Arrival Gap Logic (Physical Disruption)...\n");

    /* 1. Warm up: Establish Lock */
    for (int i = 0; i < 30; i++) {
        make_pcr_packet(pkt, pcr_val);
        tsa_clock_update(pkt, &inspector, now_ns, true);
        pcr_val += 540000;     /* 20ms content gap */
        now_ns += 20000000ULL; /* 20ms wall clock */
    }
    assert(inspector.state == TSA_CLOCK_STATE_LOCKED);
    printf("[PASS] Initialization: Locked.\n");

    /* 2. Scenario: Physical Gap (500ms) but Continuous PCR (20ms)
     * This simulates 'kill -STOP' on the sender. */
    now_ns += 500000000ULL; /* 500ms physical gap */
    pcr_val += 540000;      /* BUT ONLY 20ms PCR gap (perfect content) */

    make_pcr_packet(pkt, pcr_val);
    tsa_clock_update(pkt, &inspector, now_ns, true);

    printf("[INFO] Errors after physical gap: %u\n", inspector.priority_1_errors);
    if (inspector.priority_1_errors != 1) {
        printf("[FAIL] Physical gap was NOT captured as an error.\n");
        return 1;
    }
    printf("[PASS] Physical Arrival Gap captured correctly.\n");

    /* 3. Scenario: Burst Recovery
     * Send 10 packets immediately after. Error count should NOT increase. */
    for (int i = 0; i < 10; i++) {
        now_ns += 100000ULL; /* 100us burst */
        pcr_val += 540000;
        make_pcr_packet(pkt, pcr_val);
        tsa_clock_update(pkt, &inspector, now_ns, true);
    }
    assert(inspector.priority_1_errors == 1);
    printf("[PASS] Burst Recovery: Still exactly 1 error.\n");

    printf("\n>>> PCR ARRIVAL TEST PASSED <<<\n");
    return 0;
}
