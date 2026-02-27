#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include "tsa_internal.h"

void test_perfect_sync() {
    ts_pts_pcr_window_t w;
    ts_pts_pcr_window_init(&w, 100);

    // Simulate 1 second of video at 50fps
    // PCR clock runs at 1.0 speed
    // PTS clock runs at 1.0 speed
    for (int i = 0; i < 50; i++) {
        int128_t pcr_ns = i * 20000000ULL;        // 20ms interval
        int128_t pts_ns = pcr_ns + 100000000ULL;  // Constant 100ms offset (VBV delay)
        ts_pts_pcr_window_add(&w, pcr_ns, pts_ns);
    }

    q64_64 slope;
    int res = ts_pts_pcr_window_regress(&w, &slope);
    assert(res == 0);

    double slope_d = (double)slope / (double)((int128_t)1 << Q_SHIFT);
    double ppm = (slope_d - 1.0) * 1e6;

    printf("Perfect Sync PPM: %.4f\n", ppm);
    assert(fabs(ppm) < 0.1);  // Should be very close to 0

    ts_pts_pcr_window_destroy(&w);
}

void test_drift() {
    ts_pts_pcr_window_t w;
    ts_pts_pcr_window_init(&w, 100);

    // PTS runs 100 PPM faster
    // 100 PPM = 1e-4 drift
    double drift = 1.0 + 100.0 / 1e6;

    for (int i = 0; i < 50; i++) {
        int128_t pcr_ns = i * 20000000ULL;
        int128_t pts_ns = (int128_t)(pcr_ns * drift) + 100000000ULL;
        ts_pts_pcr_window_add(&w, pcr_ns, pts_ns);
    }

    q64_64 slope;
    ts_pts_pcr_window_regress(&w, &slope);

    double slope_d = (double)slope / (double)((int128_t)1 << Q_SHIFT);
    double ppm = (slope_d - 1.0) * 1e6;

    printf("Drift 100 PPM: %.4f\n", ppm);
    assert(fabs(ppm - 100.0) < 1.0);  // Allow small error due to precision

    ts_pts_pcr_window_destroy(&w);
}

int main() {
    printf("Testing PTS Trajectory...\n");
    test_perfect_sync();
    test_drift();
    printf("PTS Trajectory Tests Passed\n");
    return 0;
}
