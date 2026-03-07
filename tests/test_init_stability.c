#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tsa.h"
#include "tsa_internal.h"

/**
 * Test: Initialization Stability
 * Purpose: Verify that bitrate and health score remain sane during the first few seconds
 * of stream ingestion, even with frequent snapshot calls.
 */
void test_bitrate_spike_suppression() {
    printf("Running Bitrate Spike Suppression Test...\n");
    tsa_config_t cfg = {0};
    tsa_handle_t* h = tsa_create(&cfg);

    uint8_t pkt[188] = {0};
    pkt[0] = 0x47;
    pkt[1] = 0x00;  // PID 0 (PAT)

    /* Simulate a burst of 1000 packets arriving at T=1s */
    uint64_t now = NS_PER_SEC;
    for (int i = 0; i < 1000; i++) {
        tsa_process_packet(h, pkt, now + (i * 1000));  // Very fast packets
    }

    /* Take a snapshot immediately (dt will be very small) */
    tsa_commit_snapshot(h, now + 1000000ULL);  // 1ms later

    tsa_snapshot_full_t snap;
    tsa_take_snapshot_full(h, &snap);

    printf("Initial Physical Bitrate: %lu bps (%.2f Mbps)\n", snap.summary.physical_bitrate_bps,
           (double)snap.summary.physical_bitrate_bps / 1000000.0);

    /* Professional Expectation: Bitrate should be capped or smoothed, not 1.5Gbps+ */
    assert(snap.summary.physical_bitrate_bps < TS_MAX_BITRATE_BPS);
    /* In a 1ms window, 1000 packets = 1000 * 1504 * 1000 = 1.5 Gbps.
     * We want to see if our EMA or safety caps work. */

    tsa_destroy(h);
}

void test_initial_timeout_suppression() {
    printf("Running Initial Timeout Suppression Test...\n");
    tsa_config_t cfg = {0};
    tsa_handle_t* h = tsa_create(&cfg);

    /* No packets received yet. Snapshot at T=1s */
    tsa_commit_snapshot(h, NS_PER_SEC);

    tsa_snapshot_full_t snap;
    tsa_take_snapshot_full(h, &snap);

    /* Even with no packets, health score should not be 0 yet (Warm-up phase) */
    printf("Initial Health Score: %.1f\n", snap.summary.master_health);

    /* After 1 second of silence, we might expect a timeout,
     * but professional engines usually allow a 2-5s grace period. */
    assert(snap.summary.master_health > 50.0);

    tsa_destroy(h);
}

int main() {
    test_bitrate_spike_suppression();
    test_initial_timeout_suppression();
    printf("Initialization stability tests passed!\n");
    return 0;
}
