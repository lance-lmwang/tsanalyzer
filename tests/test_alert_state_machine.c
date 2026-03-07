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

    // Explicitly clear signal_lock to simulate total loss
    h->signal_lock = false;

    // Trigger CC Error during Sync Loss
    tsa_push_event(h, TSA_EVENT_CC_ERROR, 100, 0);

    // CC should NOT be FIRING because tsa_push_event suppresses it when !signal_lock
    printf("  Checking suppression: CC status is %d (expected %d)\n", h->alerts[TSA_ALERT_CC].status,
           TSA_ALERT_STATE_OFF);
    assert(h->alerts[TSA_ALERT_CC].status == TSA_ALERT_STATE_OFF);

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
