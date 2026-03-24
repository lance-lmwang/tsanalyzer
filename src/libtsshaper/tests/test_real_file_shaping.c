#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "tsshaper/tsshaper.h"

#define TS_PACKET_SIZE 188
#define TARGET_BITRATE 5000000 // 5Mbps
#define TEST_DURATION_SEC 10

int main() {
    printf("Starting Real File Smoothing Test (sample/test_1m.ts -> 5Mbps CBR)...\n");

    FILE* in_f = fopen("../../sample/test_1m.ts", "rb");
    if (!in_f) {
        perror("Failed to open sample/test_1m.ts");
        return 1;
    }

    FILE* out_f = fopen("shaper_real_test.ts", "wb");
    assert(out_f != NULL);

    tsshaper_config_t config = {0};
    config.bitrate_bps = TARGET_BITRATE;
    tsshaper_t* shaper = tsshaper_create(&config);
    assert(shaper != NULL);

    // T-STD CONFIGURATION:
    // Video Source is ~1Mbps. We set leak rate to 1.5Mbps to handle VBR peaks
    // while enforcing strict smoothing to protect the 512-byte TB.
    // The remaining 3.5Mbps will be filled with NULL packets.
    tsshaper_set_pid_bitrate(shaper, 0x1500, 1500000);
    tsshaper_set_pid_bitrate(shaper, 0x1501, 200000); // 200kbps for Audio

    uint64_t total_packets_to_pull = (uint64_t)TARGET_BITRATE * TEST_DURATION_SEC / 8 / 188;
    uint8_t pkt[TS_PACKET_SIZE];
    uint64_t pulled = 0;

    printf("Processing %lu packets...\n", total_packets_to_pull);

    uint8_t pending_pkt[TS_PACKET_SIZE];
    bool has_pending = false;

    while (pulled < total_packets_to_pull) {
        // 1. Prepare a packet to push
        if (!has_pending) {
            if (fread(pending_pkt, 1, TS_PACKET_SIZE, in_f) != TS_PACKET_SIZE) {
                fseek(in_f, 0, SEEK_SET);
                fread(pending_pkt, 1, TS_PACKET_SIZE, in_f);
            }
            has_pending = true;
        }

        // 2. Try to push it
        uint16_t pid = ((pending_pkt[1] & 0x1F) << 8) | pending_pkt[2];
        if (tsshaper_push(shaper, pid, pending_pkt, 0) == 0) {
            has_pending = false; // Successfully pushed
        } else {
            // Queue is full (Backpressure). We must PULL to make space.
            // Do not discard pending_pkt!
            // if (pulled % 1000 == 0) printf("Push blocked at %lu\n", pulled);
        }

        // 3. Pull one packet out (Synchronous / Virtual Time)
        // This advances virtual time and consumes from the queue (if allowed by shaper)
        uint8_t out_pkt[TS_PACKET_SIZE];
        tsshaper_pull(shaper, out_pkt);
        fwrite(out_pkt, 1, TS_PACKET_SIZE, out_f);
        pulled++;

        if (pulled % 5000 == 0) printf("Progress: %lu/%lu...\n", pulled, total_packets_to_pull);
    }

    fclose(in_f);
    fclose(out_f);
    tsshaper_destroy(shaper);

    printf("Saved to shaper_real_test.ts. Result should be strict 5Mbps CBR.\n");
    return 0;
}
