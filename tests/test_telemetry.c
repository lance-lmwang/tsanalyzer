#define _GNU_SOURCE
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "tsp.h"

void test_pps_telemetry() {
    setenv("TSPACER_SKIP_HARDENING", "1", 1);

    tsp_config_t cfg = {0};
    cfg.bitrate = 10000000;  // 10 Mbps
    cfg.dest_ip = "127.0.0.1";
    cfg.port = 1234;
    cfg.ts_per_udp = 7;
    cfg.mode = TSPACER_MODE_BASIC;

    tsp_handle_t* h = tsp_create(&cfg);
    assert(h != NULL);

    if (tsp_start(h) != 0) {
        perror("tsp_start failed");
        tsp_destroy(h);
        return;
    }

    uint8_t payload[188] = {0x47, 0x1f, 0xff, 0x10};

    printf("Sending packets for 2 seconds...\n");
    for (int i = 0; i < 5000; i++) {
        tsp_enqueue(h, payload, 1);
        if (i % 100 == 0) usleep(10000);
    }

    sleep(2);

    uint64_t total, pps, det_rate, drops;
    int64_t max_j, min_j;
    tsp_get_stats(h, &total, &max_j, &min_j, &drops, &det_rate, &pps);

    printf("Total Packets: %lu, PPS: %lu\n", total, pps);

    assert(pps > 0);

    tsp_destroy(h);
    printf("Telemetry tests passed!\n");
}

int main() {
    test_pps_telemetry();
    return 0;
}
