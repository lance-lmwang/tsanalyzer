#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tsa.h"
#include "tsa_internal.h"

// API to be implemented
void tsa_forensic_generate_json(tsa_handle_t* h, char* buffer, size_t size);

void test_forensic_json_basic() {
    printf("Testing forensic.json generation...\n");
    tsa_config_t cfg = {0};
    tsa_handle_t* h = tsa_create(&cfg);
    assert(h != NULL);

    char buf[4096];
    tsa_forensic_generate_json(h, buf, sizeof(buf));

    printf("Generated JSON: %s\n", buf);

    // Basic JSON checks
    assert(strstr(buf, "\"engine_version\"") != NULL);
    assert(strstr(buf, "\"state_hash\"") != NULL);
    assert(strstr(buf, "\"rca_scores\"") != NULL);
    assert(strstr(buf, "\"predictive\"") != NULL);
    assert(strstr(buf, "\"health\"") != NULL);
    assert(strstr(buf, "\"load_pct\"") != NULL);
    assert(strstr(buf, "\"processing_latency_us\"") != NULL);

    tsa_destroy(h);
    printf("Forensic JSON test passed.\n");
}

int main() {
    test_forensic_json_basic();
    return 0;
}
