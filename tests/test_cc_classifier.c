#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "tsa_internal.h"

void test_cc_classification() {
    printf("Testing CC error classification...\n");

    // Standard increment (No error)
    assert(cc_classify_error(0, 1, true, false) == TS_CC_OK);
    assert(cc_classify_error(15, 0, true, false) == TS_CC_OK);

    // Duplicate
    assert(cc_classify_error(5, 5, true, false) == TS_CC_DUPLICATE);

    // Packet Loss
    assert(cc_classify_error(0, 2, true, false) == TS_CC_LOSS);
    assert(cc_classify_error(14, 0, true, false) == TS_CC_LOSS);

    // Out-of-Order
    assert(cc_classify_error(5, 3, true, false) == TS_CC_OUT_OF_ORDER);

    // Discontinuity
    assert(cc_classify_error(0, 10, true, true) == TS_CC_OK);

    printf("CC error classification tests passed!\n");
}

int main() {
    test_cc_classification();
    return 0;
}
