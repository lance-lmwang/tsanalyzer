#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>

#include "tsa_internal.h"

void test_entropy_calculation() {
    printf("Testing Shannon Entropy calculation...\n");

    // Case 1: Single PID (0x1FFF) - Zero or very low entropy
    uint32_t counts_low[8192] = {0};
    counts_low[0x1FFF] = 500;
    double h_low = calculate_shannon_entropy(counts_low, 500);
    printf("Low entropy (Single PID): %f\n", h_low);
    assert(h_low < 0.1);

    // Case 2: Multi-PID (Uniform distribution) - High entropy
    uint32_t counts_high[8192] = {0};
    for (int i = 0; i < 10; i++) counts_high[i] = 50;
    double h_high = calculate_shannon_entropy(counts_high, 500);
    printf("High entropy (10 PIDs): %f\n", h_high);
    assert(h_high > 3.0);
}

int main() {
    test_entropy_calculation();
    printf("Shannon Entropy tests passed!\n");
    return 0;
}
