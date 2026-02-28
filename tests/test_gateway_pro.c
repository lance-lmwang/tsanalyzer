#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tsa.h"
#include "tsa_internal.h"
#include "tsp.h"

void test_gateway_throttling() {
    printf("Running test_gateway_throttling...\n");
    tsa_gateway_config_t cfg = {0};
    cfg.pacing.bitrate = 10000000;
    cfg.pacing.ts_per_udp = 7;
    cfg.pacing.mode = TSPACER_MODE_CBR;  // Use CBR to be safe

    printf("Local Config Bitrate: %lu\n", cfg.pacing.bitrate);

    tsa_gateway_t* gw = tsa_gateway_create(&cfg);
    assert(gw != NULL);

    tsp_handle_t* tsp = (tsp_handle_t*)tsa_gateway_get_tsp_handle(gw);
    uint64_t br = tsp_get_bitrate(tsp);
    printf("TSP Handle Bitrate: %lu\n", br);

    if (br != 10000000) {
        printf("ERROR: Bitrate mismatch! Expected 10000000, got %lu\n", br);
    }
    assert(br == 10000000);

    tsa_gateway_destroy(gw);
    printf("test_gateway_throttling passed.\n");
}

int main() {
    test_gateway_throttling();
    return 0;
}
