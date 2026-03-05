#include <assert.h>
#include <stdio.h>

#include "tsa_internal.h"

void test_sliding_window() {
    printf("Testing PCR sliding window management...\n");

    ts_pcr_window_t w;
    ts_pcr_window_init(&w, 4);

    assert(w.count == 0);

    ts_pcr_window_add(&w, 100, 1000, 10);
    assert(w.count == 1);
    assert(w.samples[0].sys_ns == 100);

    ts_pcr_window_add(&w, 200, 2000, 20);
    ts_pcr_window_add(&w, 300, 3000, 30);
    ts_pcr_window_add(&w, 400, 4000, 40);
    assert(w.count == 4);
    assert(w.head == 0);

    ts_pcr_window_add(&w, 500, 5000, 50);
    assert(w.count == 4);
    assert(w.head == 1);
    assert(w.samples[0].sys_ns == 500);

    ts_pcr_window_destroy(&w);

    printf("PCR sliding window tests passed.\n");
}

void test_regression() {
    printf("Testing PCR slope regression...\n");

    ts_pcr_window_t w;
    ts_pcr_window_init(&w, 10);

    // Perfectly linear sequence: y = 2x + 100
    for (int i = 0; i < 10; i++) {
        ts_pcr_window_add(&w, i * 1000, i * 2000 + 100, i * 5);
    }

    double slope;
    double intercept;
    int ret = ts_pcr_window_regress(&w, &slope, &intercept, NULL);

    assert(ret == 0);
    // Slope should be 2.0
    assert(slope > 1.99 && slope < 2.01);
    assert(intercept > 99.0 && intercept < 101.0);

    ts_pcr_window_destroy(&w);
    printf("PCR slope regression tests passed.\n");
}

void test_jitter() {
    printf("Testing PCR jitter regression...\n");

    ts_pcr_window_t w;
    ts_pcr_window_init(&w, 10);

    // Line: y = 1x + 0, but the last point has a 50ns jitter
    for (int i = 0; i < 9; i++) {
        ts_pcr_window_add(&w, i * 1000, i * 1000, i);
    }
    // Add point with jitter
    int128_t sys_now = 9 * 1000;
    int128_t pcr_now = 9 * 1000 + 50;
    ts_pcr_window_add(&w, sys_now, pcr_now, 9);

    double slope;
    double intercept;
    ts_pcr_window_regress(&w, &slope, &intercept, NULL);

    int128_t predicted = (int128_t)(slope * sys_now + intercept);
    int64_t residual = (int64_t)(pcr_now - predicted);

    // Residual should be positive and roughly 50ns (minus the slope pull)
    assert(residual > 0 && residual < 60);

    ts_pcr_window_destroy(&w);
    printf("PCR jitter regression tests passed.\n");
}

int main() {
    test_sliding_window();
    test_regression();
    test_jitter();
    return 0;
}
