#include <assert.h>
#include <stdio.h>
#include <unistd.h>

#include "tsa_histogram.h"

void test_burst_detection() {
    printf("Testing Bitrate Peak Detection...\n");
    tsa_histogram_t h;
    tsa_hist_init(&h);

    uint64_t now = 1000000000ULL;  // 1s

    // 1. Simulate 100ms burst at 50 Mbps
    // 50 Mbps = 50,000,000 bits/sec = 50,000 bits per ms
    // For 10ms bucket, that's 500,000 bits
    for (int i = 0; i < 10; i++) {
        tsa_hist_add_packet(&h, now, 500000);
        now += 10000000ULL;  // +10ms
    }

    // 2. Simulate 900ms silence
    now += 900000000ULL;
    tsa_hist_update(&h, now);

    printf("  Peak Bitrate: %.2f Mbps\n", (double)h.max_bps / 1000000.0);
    printf("  Avg Bitrate (1s window): %.2f Mbps\n",
           (double)h.p99_bps / 1000000.0);  // p99 will be 0 here since most buckets are empty

    // Peak should be 50 Mbps
    assert(h.max_bps >= 49000000 && h.max_bps <= 51000000);

    // Min should be 0 because of silence
    assert(h.min_bps == 0);

    printf("  [PASS] Peak detection validated.\n");
}

int main() {
    test_burst_detection();
    printf("ALL BITRATE PEAK TESTS PASSED\n");
    return 0;
}
