#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include "tsshaper/tsshaper.h"

// Simple timestamp for logging
static double get_timestamp() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}

int main(int argc, char* argv[]) {
    if (argc < 4) {
        printf("Usage: %s <input.ts> <output.ts> <bitrate_bps> [loop_count]\n", argv[0]);
        return 1;
    }

    const char* input_path = argv[1];
    const char* output_path = argv[2];
    uint64_t bitrate = strtoull(argv[3], NULL, 10);
    int loop_count = (argc > 4) ? atoi(argv[4]) : 1;

    FILE* in = fopen(input_path, "rb");
    FILE* out = fopen(output_path, "wb");
    if (!in || !out) {
        perror("fopen");
        return 1;
    }

    // Initialize Shaper with strict CBR config
    tsshaper_config_t cfg = {
        .bitrate_bps = bitrate,
        .use_raw_clock = false,
        .io_batch_size = 7
    };
    tsshaper_t* shaper = tsshaper_create(&cfg);
    if (!shaper) {
        fprintf(stderr, "Failed to create shaper\n");
        return 1;
    }

    uint8_t pkt[188];
    uint64_t total_in = 0;
    uint64_t total_out = 0;
    uint64_t null_pkts = 0;
    double elapsed = 0, out_mbps = 0;
    double start_time = get_timestamp();

    printf("[OFFLINE] Shaping %s -> %s\n", input_path, output_path);
    printf("[OFFLINE] Target Bitrate: %lu bps\n", bitrate);
    printf("[OFFLINE] Loops: %d\n", loop_count);

    for (int l = 0; l < loop_count; l++) {
        fseek(in, 0, SEEK_SET);
        while (fread(pkt, 1, 188, in) == 188) {
            uint16_t pid = ((pkt[1] & 0x1F) << 8) | pkt[2];

            // Push logic with backpressure handling
            int retry = 0;
            while (tsshaper_push(shaper, pid, pkt, 0) != 0) {
                // Shaper is full, we MUST pull to make space
                uint8_t out_pkt[188];
                tsshaper_pull(shaper, out_pkt);
                fwrite(out_pkt, 1, 188, out);
                total_out++;

                uint16_t out_pid = ((out_pkt[1] & 0x1F) << 8) | out_pkt[2];
                if (out_pid == 0x1FFF) null_pkts++;

                retry++;
                if (retry > 50000) {
                    fprintf(stderr, "FATAL: Shaper deadlock at in_pkt %lu. Check queue sizes.\n", total_in);
                    goto cleanup;
                }
            }
            total_in++;
        }
    }

    // Drain the shaper
    printf("[OFFLINE] Input consumed. Draining shaper queues...\n");
    while (!tsshaper_is_empty(shaper)) {
        uint8_t out_pkt[188];
        tsshaper_pull(shaper, out_pkt);

        uint16_t out_pid = ((out_pkt[1] & 0x1F) << 8) | out_pkt[2];
        fwrite(out_pkt, 1, 188, out);
        total_out++;
        if (out_pid == 0x1FFF) null_pkts++;
    }

cleanup:
    elapsed = get_timestamp() - start_time;
    if (elapsed > 0) {
        out_mbps = (double)total_out * 188 * 8 / elapsed / 1000000.0;
    }

    printf("[OFFLINE] Done.\n");
    printf("  Total Input : %lu pkts\n", total_in);
    printf("  Total Output: %lu pkts\n", total_out);
    printf("  Null Packets: %lu (%.1f%%)\n", null_pkts, (double)null_pkts * 100.0 / total_out);
    printf("  Processing Speed: %.2f Mbps\n", out_mbps);

    // Validate strict CBR constraint: Output must be >= Input
    if (total_out < total_in) {
        printf("WARNING: Output packets < Input packets. Possible loss or misconfiguration.\n");
    }

    tsshaper_destroy(shaper);
    fclose(in);
    fclose(out);
    return 0;
}
