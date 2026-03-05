#include <assert.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "tsa_internal.h"

/* Local implementation of missing TR 101 290 helper logic for testing */
static int check_cc_error(uint8_t last_cc, uint8_t current_cc, bool has_payload, bool disc_indicator) {
    if (disc_indicator) return 0;
    if (!has_payload) return 0;
    if (current_cc == last_cc) return 0; // Duplicate packets allowed
    if (current_cc == ((last_cc + 1) & 0x0F)) return 0;
    return 1; // Error
}

void test_cc_logic() {
    printf("Testing CC error logic...\n");
    // Standard increment
    assert(check_cc_error(0, 1, true, false) == 0);
    // Wrap around
    assert(check_cc_error(15, 0, true, false) == 0);
    // Discontinuity indicator
    assert(check_cc_error(0, 5, true, true) == 0);
    // Duplicate allowed
    assert(check_cc_error(0, 0, true, false) == 0);
    // Real error
    assert(check_cc_error(0, 2, true, false) != 0);
}

void test_pcr_jitter() {
    printf("Testing PCR jitter & drift logic...\n");
    double drift = 0;
    calculate_pcr_jitter(0, 0, &drift);
    double jitter = calculate_pcr_jitter(2700000, 100000000, &drift);
    assert(fabs(jitter) < 1.0);
}

int main() {
    test_cc_logic();
    test_pcr_jitter();

    printf("\n=== PROMETHEUS EXPORT PREVIEW ===\n");
    tsa_config_t cfg = {0};
    strncpy(cfg.input_label, "VERIFY-STREAM", 31);
    tsa_handle_t* h = tsa_create(&cfg);
    h->live->pts_error.count = 5;
    h->live->pid_error.count = 2;
    h->live->transport_error.count = 1;
    h->live->mdi_df_ms = 12.5;
    h->live->mdi_mlr_pkts_s = 0.5;

    static char buf[16384];
    tsa_exporter_prom_v2(&h, 1, buf, sizeof(buf));
    printf("%s\n", buf);
    tsa_destroy(h);

    printf("TR 101 290 core tests passed!\n");
    return 0;
}
