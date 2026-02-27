#include <assert.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "tsa_internal.h"

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
    printf("TR 101 290 core tests passed!\n");
    return 0;
}
