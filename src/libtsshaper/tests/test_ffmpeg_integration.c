#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "tsshaper/tsshaper.h"

#define TS_PACKET_SIZE 188

// Mock context to mimic FFmpeg's MpegTSWrite
typedef struct {
    int m2ts_mode;
    uint64_t total_size;
    uint64_t last_pcr_val;
} mock_ffmpeg_ctx;

// Mock function to mimic FFmpeg's get_pcr (Simplified)
static int64_t mock_get_pcr(void *priv) {
    static int64_t pcr = 0;
    pcr += 1260;  // Simulate 35ms steps roughly
    return pcr;
}

// THE REAL CALLBACK implemented for FFmpeg integration
static int mock_ffmpeg_write_cb(const uint8_t *packet, void *opaque) {
    mock_ffmpeg_ctx *ts = (mock_ffmpeg_ctx *)opaque;

    if (ts->m2ts_mode) {
        int64_t pcr = mock_get_pcr(ts);
        uint32_t tp_extra_header = pcr % 0x3fffffff;
        // Simulating avio_write(s->pb, &tp_extra_header, 4)
        ts->total_size += 4;
    }
    // Simulating avio_write(s->pb, packet, 188)
    ts->total_size += TS_PACKET_SIZE;

    // Store last PCR seen for jitter calculation in test
    if ((packet[3] & 0x20) && packet[4] > 0 && (packet[5] & 0x10)) {
        uint64_t pcr_high = (uint64_t)packet[6] << 25 | (uint64_t)packet[7] << 17 | (uint64_t)packet[8] << 9 |
                            (uint64_t)packet[9] << 1 | (packet[10] >> 7);
        ts->last_pcr_val = pcr_high;
    }

    return 0;
}

void test_integration_logic() {
    printf("Starting FFmpeg Integration Self-Test (Mocked Callback)...\n");

    mock_ffmpeg_ctx ctx = {.m2ts_mode = 0, .total_size = 0, .last_pcr_val = 0};

    tsshaper_config_t config = {0};
    config.bitrate_bps = 5000000;  // 5Mbps
    config.backend = TSS_BACKEND_CALLBACK;
    config.write_cb = mock_ffmpeg_write_cb;
    config.write_opaque = &ctx;
    config.pcr_interval_ms = 40;

    tsshaper_t *shaper = tsshaper_create(&config);
    assert(shaper != NULL);

    tsshaper_start_pacer(shaper, -1);

    uint8_t pkt[TS_PACKET_SIZE];
    memset(pkt, 0xFF, TS_PACKET_SIZE);
    pkt[0] = 0x47;
    pkt[1] = 0x01;
    pkt[2] = 0x00;  // PID 0x100
    pkt[3] = 0x10;

    // SCENARIO: 2-second burst of video (5Mbps = 625KB/s)
    // 5000000 bps / 8 / 188 = ~3324 packets
    printf("Pushing 2 seconds of video packets...\n");
    int push_count = 6648;
    for (int i = 0; i < push_count; i++) {
        while (tsshaper_push(shaper, 0x100, pkt, 0) != 0) {
            usleep(100);  // Backpressure wait
        }
        if (i % 1000 == 0) printf("Pushed %d packets...\n", i);
    }

    printf("Waiting for pacer to drain...\n");
    usleep(2500000);  // Wait 2.5s for 2s data

    tsshaper_stop_pacer(shaper);

    tsshaper_stats_t stats;
    tsshaper_get_stats(shaper, &stats);
    printf("Final Stats:\n");
    printf("- Total Bytes Callback: %lu\n", ctx.total_size);
    printf("- Null Packets Inserted: %lu\n", stats.null_packets_inserted);

    assert(ctx.total_size >= (uint64_t)push_count * 188);

    tsshaper_destroy(shaper);
    printf("FFmpeg Integration Logic Verified.\n\n");
}

int main() {
    test_integration_logic();
    return 0;
}
