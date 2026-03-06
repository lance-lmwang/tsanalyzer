#define _GNU_SOURCE
#include <assert.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "tsp.h"

static atomic_int g_callback_count = 0;
static tsp_stats_t g_last_stats = {0};

void my_stats_cb(tsp_handle_t* h, const tsp_stats_t* stats, void* user_data) {
    (void)h;
    (void)user_data;
    g_last_stats = *stats;
    atomic_fetch_add(&g_callback_count, 1);
    printf("Callback received: packets=%lu, pps=%lu\n", stats->total_packets, stats->pps);
}

int main() {
    setenv("TSPACER_SKIP_HARDENING", "1", 1);

    tsp_config_t cfg = {0};
    cfg.bitrate = 1000000;  // 1 Mbps
    cfg.dest_ip = "127.0.0.1";
    cfg.port = 1234;
    cfg.ts_per_udp = 7;
    cfg.mode = TSPACER_MODE_BASIC;
    cfg.stats_cb = my_stats_cb;

    tsp_handle_t* h = tsp_create(&cfg);
    assert(h != NULL);

    if (tsp_start(h) != 0) {
        perror("tsp_start failed");
        return 1;
    }

    uint8_t payload[188] = {0x47, 0x1f, 0xff, 0x10};

    printf("Sending packets for 2.5 seconds...\n");
    for (int i = 0; i < 10000; i++) {
        tsp_enqueue(h, payload, 1);
        if (i % 100 == 0) usleep(1000);
    }

    sleep(3);

    int count = atomic_load(&g_callback_count);
    printf("Final callback count: %d\n", count);

    // We expect at least 2 callbacks (one per second)
    assert(count >= 2);
    assert(g_last_stats.total_packets > 0);

    // Test API cache query
    tsp_stats_t snap;
    int rc = tsp_get_stats_snapshot(h, &snap);
    (void)rc;
    assert(rc == 0);
    assert(snap.total_packets == g_last_stats.total_packets);

    tsp_destroy(h);
    printf("Tspacer callback test passed!\n");
    return 0;
}
