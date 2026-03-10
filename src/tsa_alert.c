#include "tsa_alert.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tsa_internal.h"
#include "tsa_log.h"

void tsa_alert_update(tsa_handle_t* h, tsa_alert_id_t id, bool has_error, const char* name, uint16_t pid) {
    tsa_alert_state_t* s = &h->alerts[id];
    uint64_t now = h->stc_ns;

    /* Hierarchy Suppression:
     * If SYNC_LOSS is active, suppress all other alerts EXCEPT SYNC itself. */
    if (id != TSA_ALERT_SYNC && h->alerts[TSA_ALERT_SYNC].status == TSA_ALERT_STATE_FIRING) {
        return;
    }

    if (has_error) {
        s->last_occurrence_ns = now;
        s->count_in_window++;

        if (s->status == TSA_ALERT_STATE_OFF) {
            s->status = TSA_ALERT_STATE_FIRING;
            s->needs_resolve_msg = true;

            tsa_info("ALERT", "FIRING: %s Error detected on PID 0x%04x (stream=%s)", name, pid,
                     h->config.input_label[0] ? h->config.input_label : "unknown");

            /* Check filter mask before sending webhook */
            uint64_t mask = tsa_alert_get_mask(id);
            uint64_t user_mask = h->config.alert_filter_mask;
            if (user_mask == 0) user_mask = TSA_ALERT_MASK_ALL;  // Default: All enabled

            if (h->webhook && (user_mask & mask)) {
                char msg[256];
                snprintf(msg, sizeof(msg), "CRITICAL: %s Error detected on PID %d", name, pid);
                tsa_webhook_push_event(h->webhook, h->config.input_label, name, msg);
            }
        }
    }
}

void tsa_alert_check_resolutions(tsa_handle_t* h) {
    uint64_t now = h->stc_ns;
    const uint64_t RESOLVE_TIMEOUT_NS = TSA_TR101290_PID_TIMEOUT_NS;

    // Active Watchdogs for Repetition (TR 101 290)
    if (h->signal_lock && h->stc_ns > 0) {
        if (h->last_pat_ns > 0 && (h->stc_ns - h->last_pat_ns) > TSA_TR101290_PAT_TIMEOUT_NS)
            tsa_alert_update(h, TSA_ALERT_PAT, true, "PAT", 0);

        if (h->last_pmt_ns > 0 && (h->stc_ns - h->last_pmt_ns) > TSA_TR101290_PMT_TIMEOUT_NS)
            tsa_alert_update(h, TSA_ALERT_PMT, true, "PMT", 0);

        if (h->last_sdt_ns > 0 && (h->stc_ns - h->last_sdt_ns) > TSA_TR101290_SDT_TIMEOUT_NS)
            tsa_alert_update(h, TSA_ALERT_SDT, true, "SDT", 0);

        if (h->last_nit_ns > 0 && (h->stc_ns - h->last_nit_ns) > TSA_TR101290_NIT_TIMEOUT_NS)
            tsa_alert_update(h, TSA_ALERT_NIT, true, "NIT", 0);
    }

    for (int i = 0; i < TSA_ALERT_MAX; i++) {
        tsa_alert_state_t* s = &h->alerts[i];
        if (s->status == TSA_ALERT_STATE_FIRING) {
            if (now - s->last_occurrence_ns > RESOLVE_TIMEOUT_NS) {
                s->status = TSA_ALERT_STATE_OFF;
                const char* name = tsa_alert_get_name((tsa_alert_id_t)i);

                tsa_info("ALERT", "RESOLVED: %s Error stabilized (stream=%s)", name,
                         h->config.input_label[0] ? h->config.input_label : "unknown");

                uint64_t mask = tsa_alert_get_mask((tsa_alert_id_t)i);
                uint64_t user_mask = h->config.alert_filter_mask;
                if (user_mask == 0) user_mask = TSA_ALERT_MASK_ALL;

                if (s->needs_resolve_msg && h->webhook && (user_mask & mask)) {
                    char msg[256];
                    snprintf(msg, sizeof(msg), "OK: %s Error resolved and stabilized", name);
                    tsa_webhook_push_event(h->webhook, h->config.input_label, name, msg);
                }
                s->needs_resolve_msg = false;
                s->count_in_window = 0;
            }
        }
    }
}
