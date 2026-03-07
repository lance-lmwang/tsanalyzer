#include <stdio.h>
#include <assert.h>
#include <math.h>
#include "tsa_pcr_clock.h"

#define NS_PER_SEC 1000000000ULL

void test_clock_wrap() {
    printf("Testing PCR Wrap-around...\n");
    tsa_pcr_clock_t clk;
    tsa_pcr_clock_init(&clk);

    const uint64_t MOD = (1ULL << 33) * 300;
    uint64_t pcr = MOD - 27000000; // 1s before wrap
    uint64_t sys = 1000000000;

    tsa_pcr_clock_update(&clk, pcr, sys);
    assert(clk.established);

    // Wrap forward
    pcr = 27000000; // 1s after wrap
    sys += 1000000000;
    tsa_pcr_clock_update(&clk, pcr, sys);

    uint64_t predicted_sys = tsa_pcr_clock_pcr_to_sys(&clk, pcr);
    printf("  Wrap predicted sys: %lu, Actual: %lu\n", predicted_sys, sys);
    assert(predicted_sys >= sys - 1000 && predicted_sys <= sys + 1000);
}

void test_drift_compensation() {
    printf("Testing Drift Compensation (100 PPM fast)...\n");
    tsa_pcr_clock_t clk;
    tsa_pcr_clock_init(&clk);

    uint64_t pcr = 0;
    uint64_t sys = 1000000000;

    // Initial anchor
    tsa_pcr_clock_update(&clk, pcr, sys);

    // Simulate 10 seconds of drift (stream is 100ppm fast)
    // 100ppm means in 1s (27M ticks), it actually advanced 2700 ticks extra
    for (int i = 1; i <= 10; i++) {
        sys += 1000000000;
        pcr += (27000000 + 2700); // 1s + 100ppm
        tsa_pcr_clock_update(&clk, pcr, sys);
    }

    printf("  Converged Rate Scale: %.6f (Expected: ~1.000100)\n", clk.rate_scale);
    printf("  Detected Drift: %.1f PPM\n", clk.drift_ppm);

    assert(clk.rate_scale > 1.00005 && clk.rate_scale < 1.00015);
}

int main() {
    test_clock_wrap();
    test_drift_compensation();
    printf("ALL PCR CLOCK TESTS PASSED\n");
    return 0;
}
