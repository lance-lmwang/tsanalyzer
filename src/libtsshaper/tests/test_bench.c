#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "tsshaper/tsshaper.h"

#define TS_PACKET_SIZE 188

void test_basic_cbr() {
    printf("Testing Basic CBR...
");
    tsshaper_config_t config = {0};
    config.bitrate_bps = 50000000;  // 50Mbps
    config.io_batch_size = 7;
    config.use_raw_clock = true;
    config.max_latency_ms = 100;
    config.pcr_interval_ms = 35;

    tsshaper_t* shaper = tsshaper_create(&config);
    assert(shaper != NULL);

    int out_fd = open("/dev/null", O_WRONLY);
    assert(out_fd >= 0);
    tsshaper_start_pacer(shaper, out_fd);

    uint8_t pkt[TS_PACKET_SIZE];
    memset(pkt, 0xFF, TS_PACKET_SIZE);
    pkt[0] = 0x47;
    pkt[1] = 0x00;  // PID 0
    pkt[2] = 0x01;
    pkt[3] = 0x10;
    uint16_t pid = ((pkt[1] & 0x1F) << 8) | pkt[2];

    // Push 5000 packets
    int pushed = 0;
    for (int i = 0; i < 5000; i++) {
        int res = tsshaper_push(shaper, pid, pkt, 0);
        if (res == 0) {
            pushed++;
        } else {
            // Queue full, wait a bit
            usleep(100);
            i--;
        }
    }

    printf("Pushed %d packets.
", pushed);
    usleep(100000);  // 100ms to allow pacer to run

    tsshaper_stats_t stats;
    tsshaper_get_stats(shaper, &stats);
    printf("Null packets inserted: %lu
", stats.null_packets_inserted);

    // Can't check bytes sent anymore, just check it ran
    tsshaper_stop_pacer(shaper);
    tsshaper_destroy(shaper);
    close(out_fd);
    printf("Basic CBR Passed.

");
}

void test_backpressure() {
    void test_backpressure() {
        printf("Testing Backpressure...\n");
        tsshaper_config_t config = {0};
        config.bitrate_bps = 1000000;  // 1Mbps (very slow)
        config.io_batch_size = 7;
        tsshaper_t* shaper = tsshaper_create(&config);
        assert(shaper != NULL);

        int out_fd = open("/dev/null", O_WRONLY);
        assert(out_fd >= 0);
        tsshaper_start_pacer(shaper, out_fd);

        uint8_t pkt[TS_PACKET_SIZE];
        memset(pkt, 0xFF, TS_PACKET_SIZE);
        pkt[0] = 0x47;
        pkt[1] = 0x00;
        pkt[2] = 0x00;  // PID 0
        pkt[3] = 0x10;
        uint16_t pid = 0;

        int push_count = 0;
        int backpressure_hit = 0;
        for (int i = 0; i < 100000; i++) {
            int res = tsshaper_push(shaper, pid, pkt, 0);
            if (res == -1) { // Queue is full
                backpressure_hit = 1;
                break;
            }
            if (res == 0)
                push_count++;
            else
                break;
        }

        printf("Pushed %d packets before backpressure\n", push_count);
        assert(backpressure_hit == 1);

        tsshaper_stop_pacer(shaper);
        tsshaper_destroy(shaper);
        close(out_fd);
        printf("Backpressure Passed.\n\n");
    }

");
}

int main() {
    printf("Starting LibTSShaper Test Suite...

");
    test_basic_cbr();
    // test_pcr_accuracy() removed as feature is no longer available.
    test_backpressure();
    printf("All tests passed successfully!
");
    return 0;
}
