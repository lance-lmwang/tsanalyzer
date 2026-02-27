#include <assert.h>
#include <stdio.h>

#include "tsp.h"

int main() {
    tsp_config_t cfg = {0};
    cfg.bitrate = 1000000;
    cfg.dest_ip = "127.0.0.1";
    cfg.port = 1234;
    cfg.ts_per_udp = 7;
    cfg.mode = TSPACER_MODE_BASIC;
    cfg.cpu_core = 0;

    tsp_handle_t* h = tsp_create(&cfg);
    assert(h != NULL);

    tsp_destroy(h);
    printf("Governor check test completed.\n");
    return 0;
}
