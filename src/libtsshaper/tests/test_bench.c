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

void test_virtual_pcap() {
    printf("Testing Virtual Time Domain (PCAP Output)...\n");
    tsshaper_config_t config = {0};
    config.bitrate_bps = 40000000;  // 40Mbps
    config.backend = TSS_BACKEND_VIRTUAL_PCAP;
    config.backend_params = "virtual_test.pcap";
    config.pcr_interval_ms = 35;

    tsshaper_t* shaper = tsshaper_create(&config);
    assert(shaper != NULL);
    tsshaper_set_log_callback(shaper, test_log_cb, NULL);

    // Start pacer - in virtual mode, this will run at CPU speed
    tsshaper_start_pacer(shaper, -1);

    uint8_t pkt[TS_PACKET_SIZE];
    memset(pkt, 0x55, TS_PACKET_SIZE); // Dummy payload
    pkt[0] = 0x47;
    pkt[1] = 0x01; pkt[2] = 0x00; // PID 0x100 (Video)
    pkt[3] = 0x30; // AF + Payload
    pkt[4] = 183;  // AF Length
    pkt[5] = 0x10; // PCR Flag

    // Push 1000 packets with PCRs
    for (int i = 0; i < 1000; i++) {
        // We only provide a base PCR for the first packet.
        // The engine's JIT Rewriter will automatically stamp the correct
        // physical emission time onto all subsequent packets.
        uint64_t arrival_ts = 0;
        if (i == 0) {
            uint64_t pcr_ns = 1000000000ULL; // 1s
            uint64_t ticks = (pcr_ns * 27) / 1000;
            uint64_t base = ticks / 300;
            uint16_t ext = ticks % 300;
            pkt[6] = (base >> 25) & 0xFF; pkt[7] = (base >> 17) & 0xFF;
            pkt[8] = (base >> 9) & 0xFF;  pkt[9] = (base >> 1) & 0xFF;
            pkt[10] = ((base & 0x01) << 7) | 0x7E | ((ext >> 8) & 0x01);
            pkt[11] = ext & 0xFF;
            arrival_ts = pcr_ns;
        }

        // Push into the shaper.
        // Note: For pure virtual testing, we should avoid backpressure blocking
        // by occasionally yielding if the queue is full, but with our settings
        // the virtual pacer runs instantly.
        while (tsshaper_push(shaper, 0x100, pkt, arrival_ts) != 0) {
            usleep(100);
        }
    }

    // Give the pacer thread a tiny bit of real-world time to finish the batch
    usleep(50000);

    tsshaper_stop_pacer(shaper);
    tsshaper_destroy(shaper);
    printf("Virtual PCAP Test Finished. Check virtual_test.pcap.\n\n");
}

int main() {
    printf("Starting LibTSShaper Test Suite...\n\n");
    test_basic_cbr();
    test_backpressure();
    test_virtual_pcap();
    printf("All tests passed successfully!\n");
    return 0;
}
