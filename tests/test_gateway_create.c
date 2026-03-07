#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "tsa.h"

int main() {
    tsa_gateway_config_t cfg = {0};
    // Use a UDP URL to avoid blocking on SRT connection timeout
    cfg.pacing.url = "udp://127.0.0.1:1";
    cfg.pacing.dest_ip = "127.0.0.1";
    cfg.pacing.port = 0;
    cfg.analysis_primary.is_live = true;

    printf("Attempting to create gateway (fast-fail mode)...\n");
    tsa_gateway_t* gw = tsa_gateway_create(&cfg);

    if (gw) {
        printf("SUCCESS: Gateway created.\n");
        tsa_gateway_destroy(gw);
    } else {
        printf("Gateway creation failed as expected.\n");
    }

    printf("Test finished.\n");
    return 0;
}
