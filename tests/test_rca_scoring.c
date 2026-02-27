#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include "tsa.h"
#include "tsa_internal.h"

void test_rca_scoring_basic() {
    printf("Running test_rca_scoring_basic...\n");
    tsa_config_t cfg = {0};
    tsa_handle_t* h = tsa_create(&cfg);
    assert(h != NULL);

    // Default state should be OK (0)
    tsa_snapshot_full_t snap;
    tsa_take_snapshot_full(h, &snap);
    assert(snap.predictive.fault_domain == 0);

    tsa_destroy(h);
    printf("test_rca_scoring_basic passed.\n");
}

int main() {
    test_rca_scoring_basic();
    return 0;
}
