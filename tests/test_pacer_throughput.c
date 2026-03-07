#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <assert.h>
#include <time.h>
#include "tsp.h"

#define TEST_BITRATE 8000000ULL // 8Mbps
#define TEST_DURATION_S 5

int main() {
    tsp_config_t cfg = {0};
    cfg.bitrate = TEST_BITRATE;
    cfg.dest_ip = "127.0.0.1";
    cfg.port = 19999;
    cfg.mode = TSPACER_MODE_BASIC;
    cfg.ts_per_udp = 7;

    tsp_handle_t* h = tsp_create(&cfg);
    assert(h != NULL);

    uint8_t pkts[188 * 1000];
    memset(pkts, 0, sizeof(pkts));
    for(int i=0; i<1000; i++) pkts[i*188] = 0x47;

    printf("Unit Test: Pushing %llu bps for %d seconds...\n", TEST_BITRATE, TEST_DURATION_S);

    printf("[DEBUG] Calling tsp_start(h=%p)...\n", h);
    tsp_start(h);

    uint64_t count = 0;

    // Simulate file reading at high speed
    printf("[DEBUG] Entering enqueue loop...\n");
    for(int s=0; s < TEST_DURATION_S * 100; s++) {
        // Enqueue 1000 packets at a time
        int enq = tsp_enqueue(h, pkts, 1000);
        if (enq > 0) {
            count += enq;
        }
        usleep(10000); // 10ms interval
        if (s % 100 == 0) printf("[DEBUG] Progress: %d/500\n", s);
    }

    printf("[DEBUG] Loop finished. Count=%lu. Sleeping 1s for measurement...\n", count);
    sleep(1);

    uint64_t total_pkts, pps, dr;
    // First call to initialize counters
    tsp_get_stats(h, &total_pkts, NULL, NULL, NULL, &dr, &pps);

    printf("[DEBUG] Measuring for another 1s...\n");
    sleep(1);

    // Second call to get interval measurement
    tsp_get_stats(h, &total_pkts, NULL, NULL, NULL, &dr, &pps);

    double actual_br = (double)pps * 7.0 * 188.0 * 8.0;

    printf("Test Results:\n");
    printf("  Total UDP Packets Sent: %lu\n", total_pkts);
    printf("  UDP Packets Per Second (Interval): %lu\n", pps);
    printf("  Calculated Bitrate: %.2f Mbps\n", actual_br / 1000000.0);

    printf("[DEBUG] Calling tsp_destroy...\n");
    tsp_destroy(h);

    // Verification: allow 10% margin for 8Mbps
    if (actual_br < TEST_BITRATE * 0.90) {
        printf("FAILED: Bitrate too low (Target %llu, Actual %.0f)\n", TEST_BITRATE, actual_br);
        return 1;
    }

    printf("PASSED: Pacer achieved target bitrate despite OS jitter.\n");
    return 0;
}
