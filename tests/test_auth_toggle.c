#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "tsa_auth.h"

int main() {
    printf("Testing Authentication Toggle...\n");

    // Initialize with default secret
    tsa_auth_init(NULL);

    struct mg_http_message hm = {0};
    hm.uri = mg_str("/api/v1/snapshot");
    hm.method = mg_str("GET");

    // By default, it should be enabled and fail without header
    printf("1. Verifying enabled by default (should fail)...\n");
    assert(tsa_auth_verify_request(&hm) == false);
    printf("PASSED\n");

    // Disable authentication
    printf("2. Disabling authentication (should pass)...\n");
    tsa_auth_set_enabled(false);
    assert(tsa_auth_verify_request(&hm) == true);
    printf("PASSED\n");

    // Re-enable authentication
    printf("3. Re-enabling authentication (should fail)...\n");
    tsa_auth_set_enabled(true);
    assert(tsa_auth_verify_request(&hm) == false);
    printf("PASSED\n");

    printf("Authentication Toggle Test Passed!\n");
    return 0;
}
