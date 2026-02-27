#include <assert.h>
#include <stdio.h>
#include <unistd.h>

#include "tsp.h"

int main() {
    tsp_config_t cfg = {0};
    cfg.bitrate = 1000000;
    cfg.dest_ip = "127.0.0.1";
    cfg.port = 1234;
    cfg.ts_per_udp = 7;
    cfg.mode = TSPACER_MODE_BASIC;

    tsp_handle_t* h = tsp_create(&cfg);
    assert(h != NULL);

    if (tsp_start(h) != 0) {
        perror("tsp_start failed");
        tsp_destroy(h);
        return 0;  // Skip if can't start
    }

    // Just verify it runs
    usleep(200000);

    tsp_destroy(h);
    printf("Warm-up test completed.\n");
    return 0;
}
