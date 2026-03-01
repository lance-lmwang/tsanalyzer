#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "tsa.h"
#include "tsa_internal.h"

void test_rst_encoder_drift() {
    printf("Running test_rst_encoder_drift...\n");
    tsa_config_t cfg = {0};
    cfg.is_live = true;
    tsa_handle_t* h = tsa_create(&cfg);
    assert(h != NULL);

    // Max Drift = 100ms
    // Current Drift = 10ms
    // Drift Rate = 1000ppm = 0.001
    // Remaining = 90ms / 0.001 = 90s
    h->live->pcr_accuracy_ns = 10000000;
    h->stc_slope_q64 = TO_Q64_64(1.001);

    tsa_commit_snapshot(h, h->start_ns + 1000000000ULL);

    tsa_snapshot_full_t s1;
    tsa_take_snapshot_full(h, &s1);
    float rst1 = s1.predictive.rst_encoder_s;
    printf("Drift RST: %.2f s\n", rst1);

    assert(rst1 > 89.0 && rst1 < 91.0);

    tsa_destroy(h);
    printf("test_rst_encoder_drift passed.\n");
}

int main() {
    test_rst_encoder_drift();
    return 0;
}
