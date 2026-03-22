#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "tsshaper/tsshaper.h"

#define TS_PACKET_SIZE 188

// Simple log callback for testing
void test_log_cb(tss_log_level_t level, const char* msg, void* opaque) {
    const char* level_str = "UNK";
    switch (level) {
        case TSS_LOG_ERROR:
            level_str = "ERR";
            break;
        case TSS_LOG_WARN:
            level_str = "WRN";
            break;
        case TSS_LOG_INFO:
            level_str = "INF";
            break;
        case TSS_LOG_DEBUG:
            level_str = "DBG";
            break;
        case TSS_LOG_TRACE:
            level_str = "TRC";
            break;
    }
    printf("[%s] %s\n", level_str, msg);
}

void test_basic_cbr() {
    printf("Testing Basic CBR...\n");
    tsshaper_config_t config = {0};
    config.bitrate_bps = 50000000;  // 50Mbps
    config.io_batch_size = 7;
    config.use_raw_clock = true;
    config.max_latency_ms = 100;
    config.pcr_interval_ms = 35;

    tsshaper_t* shaper = tsshaper_create(&config);
    assert(shaper != NULL);

    tsshaper_set_log_callback(shaper, test_log_cb, NULL);

    // We need to manually simulate pacer if we don't have write permissions to high prio
    // But tsshaper_start_pacer just launches a thread.
    // Use /dev/null for output
    int out_fd = open("/dev/null", O_WRONLY);
    if (out_fd < 0) {
        perror("open /dev/null");
        return;
    }

    tsshaper_start_pacer(shaper, out_fd);

    uint8_t pkt[TS_PACKET_SIZE];
    memset(pkt, 0xFF, TS_PACKET_SIZE);
    pkt[0] = 0x47;
    pkt[1] = 0x00;
    pkt[2] = 0x10;  // PID 16
    pkt[3] = 0x10;
    uint16_t pid = 16;

    // Push packets
    int pushed = 0;
    for (int i = 0; i < 1000; i++) {
        int res = tsshaper_push(shaper, pid, pkt, 0);
        if (res == 0) {
            pushed++;
        } else {
            usleep(100);
            i--;  // Retry
        }
    }

    printf("Pushed %d packets.\n", pushed);
    usleep(100000);  // 100ms

    tsshaper_stats_t stats;
    tsshaper_get_stats(shaper, &stats);
    printf("Null packets inserted: %lu\n", stats.null_packets_inserted);

    tsshaper_stop_pacer(shaper);
    tsshaper_destroy(shaper);
    close(out_fd);
    printf("Basic CBR Passed.\n\n");
}

void test_backpressure() {
    printf("Testing Backpressure...\n");
    tsshaper_config_t config = {0};
    config.bitrate_bps = 1000000;  // 1Mbps (slow)
    config.io_batch_size = 7;

    tsshaper_t* shaper = tsshaper_create(&config);
    assert(shaper != NULL);
    tsshaper_set_log_callback(shaper, test_log_cb, NULL);

    int out_fd = open("/dev/null", O_WRONLY);
    if (out_fd >= 0) {
        tsshaper_start_pacer(shaper, out_fd);
    }

    uint8_t pkt[TS_PACKET_SIZE];
    memset(pkt, 0xFF, TS_PACKET_SIZE);
    pkt[0] = 0x47;
    pkt[1] = 0x00;
    pkt[2] = 0x20;  // PID 32
    pkt[3] = 0x10;
    uint16_t pid = 32;

    int backpressure_hit = 0;
    for (int i = 0; i < 200000; i++) {  // Try to flood
        int res = tsshaper_push(shaper, pid, pkt, 0);
        if (res == -1) {
            backpressure_hit = 1;
            // printf("Backpressure hit at packet %d\n", i);
            break;
        }
    }

    assert(backpressure_hit == 1);

    tsshaper_stop_pacer(shaper);
    tsshaper_destroy(shaper);
    if (out_fd >= 0) close(out_fd);
    printf("Backpressure Passed.\n\n");
}

int main() {
    printf("Starting LibTSShaper Test Suite...\n\n");
    test_basic_cbr();
    test_backpressure();
    printf("All tests passed successfully!\n");
    return 0;
}
