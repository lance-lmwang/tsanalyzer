#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "tsshaper/tsshaper.h"

#define TS_PACKET_SIZE 188
#define TARGET_BITRATE 10000000  // 10Mbps
#define TEST_DURATION_SEC 5

static FILE* g_output_file = NULL;
static uint64_t g_total_bytes = 0;

// The callback that will write to the real file
static int file_write_cb(const uint8_t* packet, void* opaque) {
    if (g_output_file) {
        fwrite(packet, 1, TS_PACKET_SIZE, g_output_file);
        g_total_bytes += TS_PACKET_SIZE;
    }
    return 0;
}

int main() {
    printf("Starting Offline Bitrate Smoothing Test (Deterministic Sync Mode)...\n");

    g_output_file = fopen("shaper_offline_test.ts", "wb");
    assert(g_output_file != NULL);

    tsshaper_config_t config = {0};
    config.bitrate_bps = TARGET_BITRATE;
    // Note: We don't start the pacer thread for this offline test.
    // We will manually pull packets to simulate the passage of time.

    tsshaper_t* shaper = tsshaper_create(&config);
    assert(shaper != NULL);

    uint8_t video_pkt[TS_PACKET_SIZE];
    memset(video_pkt, 0x55, TS_PACKET_SIZE);
    video_pkt[0] = 0x47;
    video_pkt[1] = 0x01;
    video_pkt[2] = 0x00;  // PID 0x100
    video_pkt[3] = 0x10;

    // SCENARIO: 5 seconds of total time
    uint64_t packet_interval_ns = (188ULL * 8 * 1000000000ULL) / TARGET_BITRATE;
    uint64_t packets_total = (TARGET_BITRATE * TEST_DURATION_SEC) / 8 / 188;

    printf("Generating %lu total packets at %d Mbps CBR...\n", packets_total, TARGET_BITRATE / 1000000);

    for (uint64_t p = 0; p < packets_total; p++) {
        // Every 1 second (approx), we burst 1 second's worth of video packets
        if (p % (packets_total / TEST_DURATION_SEC) == 0) {
            int burst_size = (TARGET_BITRATE / 8) / 188;
            for (int b = 0; b < burst_size; b++) {
                tsshaper_push(shaper, 0x100, video_pkt, 0);
            }
        }

        // Pull one packet (this will be either our video packet or a NULL filler)
        uint8_t out_pkt[TS_PACKET_SIZE];
        tsshaper_pull(shaper, out_pkt);
        file_write_cb(out_pkt, NULL);
    }

    fclose(g_output_file);
    tsshaper_destroy(shaper);

    printf("Saved to shaper_offline_test.ts. Total size: %lu bytes\n", g_total_bytes);
    return 0;
}
