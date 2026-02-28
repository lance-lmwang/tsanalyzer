#include <stdio.h>
#include <stdlib.h>

#include "tsa.h"

int main() {
    tsa_gateway_config_t cfg = {0};
    cfg.pacing.srt_url = "srt://:9005";
    cfg.pacing.dest_ip = "127.0.0.1";
    cfg.pacing.port = 0;
    cfg.analysis.is_live = true;
    cfg.analysis.enable_forensics = true;

    printf("Attempting to create gateway with srt://:9005...\n");
    tsa_gateway_t* gw = tsa_gateway_create(&cfg);
    if (gw) {
        printf("SUCCESS: Gateway created.\n");
        tsa_gateway_destroy(gw);
    } else {
        printf("FAILURE: Gateway creation failed.\n");
        return 1;
    }
    return 0;
}
