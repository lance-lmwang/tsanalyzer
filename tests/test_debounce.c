#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "tsa.h"
#include "tsa_internal.h"

void test_debounce_basic() {
    printf("Testing forensic debounce...\n");
    tsa_config_t cfg = {0};
    tsa_handle_t* h = tsa_create(&cfg);
    assert(h != NULL);

    // First trigger should succeed
    assert(tsa_forensic_trigger(h, 1) == true);

    // Second trigger for same domain should fail immediately
    assert(tsa_forensic_trigger(h, 1) == false);

    // Trigger for different domain should succeed
    assert(tsa_forensic_trigger(h, 2) == true);

    tsa_destroy(h);
    printf("Forensic debounce test passed.\n");
}

int main() {
    test_debounce_basic();
    return 0;
}
