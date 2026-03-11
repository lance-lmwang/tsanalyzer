#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tsa_alert.h"
#include "tsa_internal.h"

int main() {
    tsa_config_t cfg = {0};
    strncpy(cfg.input_label, "suppression_test", 31);
    cfg.alert.suppression_window_ms = 1000;  // 1 second window
    cfg.op_mode = TSA_MODE_LIVE;

    tsa_handle_t* h = tsa_create(&cfg);
    assert(h != NULL);

    printf("Testing Alert Flapping Suppression...\n");

    uint64_t now = 1000000000ULL;  // 1s
    h->stc_ns = now;

    // 1. First occurrence should NOT be suppressed
    printf("  Step 1: First occurrence (should be active)...\n");
    tsa_alert_update(h, TSA_ALERT_CC, true, "CC", 100);

    uint32_t hash_idx = (TSA_ALERT_CC * 31 + 100) % TSA_ALERT_AGGREGATOR_SIZE;
    assert(h->aggregator.entries[hash_idx].active == true);
    assert(atomic_load(&h->aggregator.entries[hash_idx].hit_count) == 1);

    sleep(1);  // Bypass logger rate limit

    // 2. Rapid flapping within 1s window
    printf("  Step 2: Flapping within window...\n");
    for (int i = 0; i < 5; i++) {
        now += 100000000ULL;  // +100ms
        h->stc_ns = now;

        // Resolve it first so we can trigger FIRING again
        h->alerts[TSA_ALERT_CC].status = TSA_ALERT_STATE_OFF;

        tsa_alert_update(h, TSA_ALERT_CC, true, "CC", 100);
    }

    // hit_count should be 1 (original) + 5 (flaps) = 6
    assert(atomic_load(&h->aggregator.entries[hash_idx].hit_count) == 6);

    // Check suppression count in snapshot
    tsa_commit_snapshot(h, h->stc_ns);
    tsa_snapshot_full_t snap;
    tsa_take_snapshot_full(h, &snap);
    printf("  Suppression count: %llu (expected 5)\n", (unsigned long long)snap.stats.alert_suppression_count);
    assert(snap.stats.alert_suppression_count == 5);

    // 3. Wait for window expiry and check for summary
    printf("  Step 3: Advancing time to expire window (+2s)...\n");
    now += 2000000000ULL;  // +2s
    h->stc_ns = now;

    // This should flush the aggregation and mark as inactive
    tsa_alert_check_resolutions(h);

    if (h->aggregator.entries[hash_idx].active) {
        printf("  Error: Aggregator entry at index %u still active!\n", hash_idx);
    }
    assert(h->aggregator.entries[hash_idx].active == false);

    sleep(1);  // Bypass logger rate limit

    // 4. New occurrence after expiry should start a new window
    printf("  Step 4: New occurrence after expiry...\n");
    h->alerts[TSA_ALERT_CC].status = TSA_ALERT_STATE_OFF;
    tsa_alert_update(h, TSA_ALERT_CC, true, "CC", 100);

    assert(h->aggregator.entries[hash_idx].active == true);
    assert(atomic_load(&h->aggregator.entries[hash_idx].hit_count) == 1);

    // 5. Test Dependency Tree Suppression
    printf("  Step 5: Testing Dependency Tree Suppression...\n");
    h->alerts[TSA_ALERT_SYNC].status = TSA_ALERT_STATE_FIRING;
    h->alerts[TSA_ALERT_CC].status = TSA_ALERT_STATE_OFF;
    tsa_alert_update(h, TSA_ALERT_CC, true, "CC", 100);
    assert(h->alerts[TSA_ALERT_CC].status == TSA_ALERT_STATE_OFF);

    h->alerts[TSA_ALERT_SYNC].status = TSA_ALERT_STATE_OFF;
    h->alerts[TSA_ALERT_CC].status = TSA_ALERT_STATE_FIRING;
    h->alerts[TSA_ALERT_PCR].status = TSA_ALERT_STATE_OFF;
    tsa_alert_update(h, TSA_ALERT_PCR, true, "PCR", 100);
    assert(h->alerts[TSA_ALERT_PCR].status == TSA_ALERT_STATE_OFF);

    // 6. Test Zero-packet timeout via Wall clock
    printf("  Step 6: Testing Zero-Packet Timeout...\n");
    h->alerts[TSA_ALERT_SYNC].status = TSA_ALERT_STATE_OFF;
    h->signal_lock = true;
    h->last_packet_rx_ns = now;
    h->last_snap_wall_ns = now + 600000000ULL;  // 600ms later, should trigger SYNC_LOSS
    tsa_alert_check_resolutions(h);
    assert(h->alerts[TSA_ALERT_SYNC].status == TSA_ALERT_STATE_FIRING);

    printf("Alert Suppression Test: PASSED\n");

    tsa_destroy(h);
    return 0;
}
