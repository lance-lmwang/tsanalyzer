#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "tsa.h"
#include "tsa_internal.h"

/* Export internal functions for testing */
extern void tsa_process_packet(tsa_handle_t* h, const uint8_t* p, uint64_t n);

static void make_packet(uint8_t* buf, uint16_t pid, uint8_t cc) {
    memset(buf, 0, 188);
    buf[0] = 0x47;
    buf[1] = (pid >> 8) & 0x1F;
    buf[2] = pid & 0xFF;
    buf[3] = 0x10 | (cc & 0x0F);
}

static void make_pcr_packet(uint8_t* buf, uint16_t pid, uint64_t pcr_ticks, uint8_t cc) {
    memset(buf, 0, 188);
    buf[0] = 0x47;
    buf[1] = (pid >> 8) & 0x1F;
    buf[2] = pid & 0xFF;
    buf[3] = 0x30 | (cc & 0x0F);
    buf[4] = 7;
    buf[5] = 0x10;
    uint64_t base = pcr_ticks / 300;
    uint16_t ext = pcr_ticks % 300;
    buf[6] = (base >> 25) & 0xFF;
    buf[7] = (base >> 17) & 0xFF;
    buf[8] = (base >> 9) & 0xFF;
    buf[9] = (base >> 1) & 0xFF;
    buf[10] = ((base & 1) << 7) | ((ext >> 8) & 0x01);
    buf[11] = ext & 0xFF;
}

int main() {
    printf(">>> STARTING PCR DRIFT UNIT TEST <<<\n");

    tsa_config_t cfg = {0};
    tsa_handle_t* h = tsa_create(&cfg);
    uint8_t pkt[188];

    /*
     * Target Scenario: +100 PPM Clock Drift
     * Interval: 1.0 second (Wall clock)
     * Correct PCR Ticks for 1s: 27,000,000
     * Drifted PCR Ticks (+100 PPM): 27,000,000 * (1 + 100/1,000,000) = 27,002,700
     */

    printf("   Step 1: Simulating +100 PPM drift stream...\n");

    // T=0 (Wall Clock)
    uint64_t t0_ns = 1000000000ULL;
    make_pcr_packet(pkt, 0x100, 0, 0);
    tsa_process_packet(h, pkt, t0_ns);

    // T=1.0s (Wall Clock)
    uint64_t t1_ns = 2000000000ULL;

    // Feed some data in between
    for (int i = 0; i < 100; i++) {
        make_packet(pkt, 0x101, i % 16);
        tsa_process_packet(h, pkt, t0_ns + (i * 10000000ULL));
    }

    // Drifted PCR
    uint64_t drifted_pcr = 27002700;
    make_pcr_packet(pkt, 0x100, drifted_pcr, 1);
    tsa_process_packet(h, pkt, t1_ns);

    double drift = h->clock_inspectors[0x100].br_est.pcr_drift_ppm;

    printf("   Measured Drift: %.2f PPM (Target: ~100.00 PPM)\n", drift);

    /* 🚨 VERIFICATION: Should be close to 100 PPM after enough samples for EMA to converge */

    for (int loop = 2; loop <= 100; loop++) {
        uint64_t wall_ns = 1000000000ULL + ((uint64_t)loop * 1000000000ULL);
        uint64_t pcr = (uint64_t)loop * 27002700ULL;
        make_pcr_packet(pkt, 0x100, pcr, loop % 16);
        tsa_process_packet(h, pkt, wall_ns);
    }

    drift = h->clock_inspectors[0x100].br_est.pcr_drift_ppm;
    printf("   Measured Drift (After 100 samples): %.2f PPM\n", drift);

    if (drift < 95.0 || drift > 105.0) {
        printf("[FAIL] Drift calculation inaccurate (converged to %.2f)!\n", drift);
        return 1;
    }

    printf(">>> PCR DRIFT TEST PASSED <<<\n");
    tsa_destroy(h);
    return 0;
}
