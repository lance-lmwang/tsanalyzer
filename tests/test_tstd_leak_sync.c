#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tsa.h"
#include "tsa_internal.h"

int main() {
    printf(">>> STARTING T-STD DYNAMIC LEAK RATE SYNC TEST <<<\n");

    tsa_config_t cfg = {0};
    tsa_handle_t* h = tsa_create(&cfg);
    assert(h != NULL);

    uint16_t pid = 0x100;
    tsa_es_track_t* es = &h->es_tracks[pid];

    // 1. Test H.264 Level 4.1 High Profile (Profile 100)
    printf("  Testing H.264 Level 4.1 High Profile...\n");
    es->stream_type = TSA_TYPE_VIDEO_H264;
    es->video.level = 41;     // Level 4.1
    es->video.profile = 100;  // High Profile
    tsa_tstd_sync_params(es);

    /* H.264 Level 4.1 Base is 50,000 kbps. High Profile adds 1.25x factor.
     * Max Bitrate = 50,000 * 1.25 = 62,500 kbps
     * Rbx = 62,500 * 1000 * 1.2 = 75,000,000 bps */
    printf("    Rbx: %llu, Rrx: %llu\n", (unsigned long long)es->tstd.leak_rate_eb,
           (unsigned long long)es->tstd.leak_rate_rx);
    assert(es->tstd.leak_rate_eb == 75000000ULL);

    // 2. Test HEVC Level 5.1 High Tier
    printf("  Testing HEVC Level 5.1 High Tier...\n");
    es->stream_type = TSA_TYPE_VIDEO_HEVC;
    es->video.level = 153;  // Level 5.1
    es->video.is_high_tier = true;
    tsa_tstd_sync_params(es);

    /* HEVC Level 5.1 Main Tier is 40,000 kbps. High Tier is 4x factor (160,000 kbps).
     * Rbx = 160,000 * 1000 * 1.2 = 192,000,000 bps */
    printf("    Rbx: %llu, Rrx: %llu\n", (unsigned long long)es->tstd.leak_rate_eb,
           (unsigned long long)es->tstd.leak_rate_rx);
    assert(es->tstd.leak_rate_eb == 192000000ULL);

    // 2.1 Test HEVC Level 4.1 High Tier (Specific factor test)
    printf("  Testing HEVC Level 4.1 High Tier...\n");
    es->video.level = 123;  // Level 4.1
    es->video.is_high_tier = true;
    tsa_tstd_sync_params(es);

    /* HEVC Level 4.1 Main Tier is 20,000 kbps. High Tier is 2.5x factor (50,000 kbps).
     * Rbx = 50,000 * 1000 * 1.2 = 60,000,000 bps */
    printf("    Rbx: %llu, Rrx: %llu\n", (unsigned long long)es->tstd.leak_rate_eb,
           (unsigned long long)es->tstd.leak_rate_rx);
    assert(es->tstd.leak_rate_eb == 60000000ULL);

    // 3. Test Fallback for unknown level
    printf("  Testing Fallback for unknown level...\n");
    es->video.level = 0;
    es->tstd.leak_rate_eb = 0;
    tsa_tstd_sync_params(es);
    assert(es->tstd.leak_rate_eb == 0);  // Should not change if unknown

    tsa_destroy(h);
    printf(">>> T-STD DYNAMIC LEAK RATE SYNC TEST PASSED <<<\n");
    return 0;
}
