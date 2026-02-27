#include <assert.h>
#include <stdbool.h>
#include <stdio.h>

#include "tsa_internal.h"

int main() {
    printf("Testing CC Classifier...\n");

    // 1. Standard increment
    assert(cc_classify_error(0, 1, true, false) == TS_CC_OK);
    assert(cc_classify_error(15, 0, true, false) == TS_CC_OK);

    // 2. Discontinuity flag (should be OK)
    assert(cc_classify_error(5, 8, true, true) == TS_CC_OK);

    // 3. Duplicate packets
    assert(cc_classify_error(5, 5, true, false) == TS_CC_DUPLICATE);
    assert(cc_classify_error(5, 5, false, false) == TS_CC_OK);  // No payload -> OK (e.g. PCR only)

    // 4. Packet Loss (forward jump 1..7)
    // 0 -> 2: Diff is 2. Lost 1 packet (CC=1).
    assert(cc_classify_error(0, 2, true, false) == TS_CC_LOSS);
    // 0 -> 7: Diff is 7. Lost 6 packets.
    assert(cc_classify_error(0, 7, true, false) == TS_CC_LOSS);

    // 14 -> 0: Diff is (0 - 14) & 0xF = 2. Lost 1 packet (CC=15).
    assert(cc_classify_error(14, 0, true, false) == TS_CC_LOSS);

    // 5. Out of Order (forward jump > 7, effectively backward or large gap)
    // 0 -> 8: Diff is 8.
    assert(cc_classify_error(0, 8, true, false) == TS_CC_OUT_OF_ORDER);
    // 0 -> 15: Diff is 15. This is logically "previous packet".
    assert(cc_classify_error(0, 15, true, false) == TS_CC_OUT_OF_ORDER);

    printf("CC Classifier Tests Passed\n");
    return 0;
}
