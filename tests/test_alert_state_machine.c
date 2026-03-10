#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "tsa.h"
#include "tsa_internal.h"

int main() {
    tsa_config_t cfg = {0};
    strncpy(cfg.input_label, "comprehensive_test", 31);
    cfg.op_mode = TSA_MODE_LIVE;

    tsa_handle_t* h = tsa_create(&cfg);
    assert(h != NULL);

    printf("Testing Comprehensive Alert State Machine...\n");

    uint64_t now = 1000000000ULL;  // 1s
    h->stc_ns = now;
    h->signal_lock = true;  // Signal must be locked to process sub-alerts

    // 1. Test PAT and PCR Independence
    printf("  Step 1: Triggering PAT and PCR Errors...\n");
    tsa_push_event(h, TSA_EVENT_PAT_TIMEOUT, 0, 0);
    tsa_push_event(h, TSA_EVENT_PCR_JITTER, 100, 50000);

    assert(h->alerts[TSA_ALERT_PAT].status == TSA_ALERT_STATE_FIRING);
    assert(h->alerts[TSA_ALERT_PCR].status == TSA_ALERT_STATE_FIRING);
    assert(h->alerts[TSA_ALERT_CC].status == TSA_ALERT_STATE_OFF);

    // 2. Test Hierarchical Suppression (SYNC_LOSS suppresses others)
    printf("  Step 2: Triggering SYNC LOSS and then CC Error...\n");
    tsa_push_event(h, TSA_EVENT_SYNC_LOSS, 0, 0);
    assert(h->alerts[TSA_ALERT_SYNC].status == TSA_ALERT_STATE_FIRING);

    // Trigger CC Error during Sync Loss
    // Even direct calls to tsa_alert_update should be suppressed when SYNC is FIRING
    tsa_alert_update(h, TSA_ALERT_CC, true, "CC", 100);

    printf("  Checking suppression: CC status is %d (expected %d)\n", h->alerts[TSA_ALERT_CC].status,
           TSA_ALERT_STATE_OFF);
    assert(h->alerts[TSA_ALERT_CC].status == TSA_ALERT_STATE_OFF);

    // 2.1 Verify Active Alerts Mask in Snapshot
    printf("  Step 2.1: Verifying active_alerts_mask in snapshot...\n");
    
    /* Ensure inherited alerts from Step 1 are still active before snapshot */
    assert(h->alerts[TSA_ALERT_PAT].status == TSA_ALERT_STATE_FIRING);
    assert(h->alerts[TSA_ALERT_PCR].status == TSA_ALERT_STATE_FIRING);

    tsa_commit_snapshot(h, h->stc_ns);
    tsa_snapshot_full_t snap;
    tsa_take_snapshot_full(h, &snap);

    uint64_t expected_mask = tsa_alert_get_mask(TSA_ALERT_SYNC) | 
                            tsa_alert_get_mask(TSA_ALERT_PAT) | 
                            tsa_alert_get_mask(TSA_ALERT_PCR);
    
    printf("  Mask check: Got 0x%llx, Expected 0x%llx\n", (unsigned long long)snap.stats.active_alerts_mask, (unsigned long long)expected_mask);
    assert(snap.stats.active_alerts_mask == expected_mask);

    // 3. Test Recovery of multiple alerts
    printf("  Step 3: Advancing time (6s) to resolve all...\n");
    h->stc_ns = now + 8000000000ULL;  // +8s

    // Simulate signal back for recovery logic
    h->signal_lock = true;

    tsa_alert_check_resolutions(h);

    assert(h->alerts[TSA_ALERT_PAT].status == TSA_ALERT_STATE_OFF);
    assert(h->alerts[TSA_ALERT_PCR].status == TSA_ALERT_STATE_OFF);
    assert(h->alerts[TSA_ALERT_SYNC].status == TSA_ALERT_STATE_OFF);

    printf("Comprehensive Alert Test: PASSED\n");

    tsa_destroy(h);
    return 0;
}
