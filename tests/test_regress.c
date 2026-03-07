#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include "tsa_internal.h"

void test_perfect_sync() {
    printf("Testing perfect sync...\n");
    ts_pcr_window_t w;
    ts_pcr_window_init(&w, 100);

    uint64_t start_sys = 1000000000ULL;
    uint64_t start_pcr = 2000000000ULL;

    for (int i = 0; i < 50; i++) {
        uint64_t sys = start_sys + i * 40000000ULL;  // 40ms intervals
        uint64_t pcr = start_pcr + i * 40000000ULL;
        ts_pcr_window_add(&w, sys, pcr, 0);
    }

    double slope, intercept;
    int64_t max_err;
    int ret = ts_pcr_window_regress(&w, &slope, &intercept, &max_err);
    (void)ret;

    assert(ret == 0);
    printf("  Slope: %f (expected 1.0)\n", slope);
    printf("  Drift: %f ppm (expected 0.0)\n", (slope - 1.0) * 1000000.0);
    printf("  Max Err: %ld ns (expected 0)\n", max_err);

    assert(fabs(slope - 1.0) < 1e-9);
    assert(max_err == 0);

    ts_pcr_window_destroy(&w);
}

void test_drift_positive() {
    printf("Testing positive drift (100 ppm)...\n");
    ts_pcr_window_t w;
    ts_pcr_window_init(&w, 100);

    uint64_t start_sys = 1000000000ULL;
    uint64_t start_pcr = 2000000000ULL;
    double drift_target_ppm = 100.0;
    double target_slope = 1.0 + (drift_target_ppm / 1000000.0);

    for (int i = 0; i < 50; i++) {
        uint64_t elapsed_sys = i * 40000000ULL;
        uint64_t elapsed_pcr = (uint64_t)(elapsed_sys * target_slope);
        ts_pcr_window_add(&w, start_sys + elapsed_sys, start_pcr + elapsed_pcr, 0);
    }

    double slope, intercept;
    int64_t max_err;
    int ret = ts_pcr_window_regress(&w, &slope, &intercept, &max_err);
    (void)ret;

    assert(ret == 0);
    double drift_calc = (slope - 1.0) * 1000000.0;
    printf("  Slope: %f\n", slope);
    printf("  Drift: %f ppm (target 100.0)\n", drift_calc);

    assert(fabs(drift_calc - 100.0) < 0.1);

    ts_pcr_window_destroy(&w);
}

void test_jitter() {
    printf("Testing jittery input...\n");
    ts_pcr_window_t w;
    ts_pcr_window_init(&w, 100);

    uint64_t start_sys = 1000000000ULL;
    uint64_t start_pcr = 2000000000ULL;

    srand(42);
    for (int i = 0; i < 100; i++) {
        uint64_t sys = start_sys + i * 40000000ULL;
        int64_t jitter = (rand() % 200000) - 100000;  // +/- 100us jitter
        uint64_t pcr = start_pcr + i * 40000000ULL + jitter;
        ts_pcr_window_add(&w, sys, pcr, 0);
    }

    double slope, intercept;
    int64_t max_err;
    int ret = ts_pcr_window_regress(&w, &slope, &intercept, &max_err);
    (void)ret;

    assert(ret == 0);
    printf("  Slope: %f\n", slope);
    printf("  Drift: %f ppm\n", (slope - 1.0) * 1000000.0);
    printf("  Max Err: %ld ns (expected approx 100000)\n", max_err);

    assert(fabs(slope - 1.0) < 0.001);  // Should still be very close to 1
    assert(max_err > 50000 && max_err < 200000);

    ts_pcr_window_destroy(&w);
}

int main() {
    test_perfect_sync();
    test_drift_positive();
    test_jitter();
    printf("ALL REGRESSION TESTS PASSED!\n");
    return 0;
}
