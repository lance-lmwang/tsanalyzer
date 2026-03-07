#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tsa_alert.h"
#include "tsa_internal.h"

static uint64_t get_mask_for_id(tsa_alert_id_t id) {
    switch (id) {
        case TSA_ALERT_SYNC:      return TSA_ALERT_MASK_P1_1_SYNC;
        case TSA_ALERT_PAT:       return TSA_ALERT_MASK_P1_3_PAT;
        case TSA_ALERT_PMT:       return TSA_ALERT_MASK_P1_5_PMT;
        case TSA_ALERT_PID:       return TSA_ALERT_MASK_P1_6_PID;
        case TSA_ALERT_CC:        return TSA_ALERT_MASK_P1_4_CC;
        case TSA_ALERT_TRANSPORT: return TSA_ALERT_MASK_P2_1_TRANSPORT;
        case TSA_ALERT_CRC:       return TSA_ALERT_MASK_P2_2_CRC;
        case TSA_ALERT_PCR:       return TSA_ALERT_MASK_P2_3_PCR;
        case TSA_ALERT_PTS:       return TSA_ALERT_MASK_P2_5_PTS;
        case TSA_ALERT_TSTD:      return TSA_ALERT_MASK_TSTD;
        case TSA_ALERT_ENTROPY:   return TSA_ALERT_MASK_ENTROPY;
        case TSA_ALERT_SDT:       return TSA_ALERT_MASK_SDT;
        case TSA_ALERT_NIT:       return TSA_ALERT_MASK_NIT;
        default:                  return 0;
    }
}

void tsa_alert_update(tsa_handle_t* h, tsa_alert_id_t id, bool has_error, const char* name, uint16_t pid) {
    tsa_alert_state_t* s = &h->alerts[id];
    uint64_t now = h->stc_ns;

    if (has_error) {
        s->last_occurrence_ns = now;
        s->count_in_window++;

        if (s->status == TSA_ALERT_STATE_OFF) {
            s->status = TSA_ALERT_STATE_FIRING;
            s->needs_resolve_msg = true;

            /* Check filter mask before sending webhook */
            uint64_t mask = get_mask_for_id(id);
            uint64_t user_mask = h->config.alert_filter_mask;
            if (user_mask == 0) user_mask = TSA_ALERT_MASK_ALL; // Default: All enabled

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
    const char* names[] = {"SYNC", "PAT", "PMT", "PID", "CC", "CRC", "PCR", "TRANSPORT", "PTS", "TSTD", "ENTROPY", "SDT", "NIT"};
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

                uint64_t mask = get_mask_for_id((tsa_alert_id_t)i);
                uint64_t user_mask = h->config.alert_filter_mask;
                if (user_mask == 0) user_mask = TSA_ALERT_MASK_ALL;

                if (s->needs_resolve_msg && h->webhook && (user_mask & mask)) {
                    char msg[256];
                    snprintf(msg, sizeof(msg), "OK: %s Error resolved and stabilized", names[i]);
                    tsa_webhook_push_event(h->webhook, h->config.input_label, names[i], msg);
                }
                s->needs_resolve_msg = false;
                s->count_in_window = 0;
            }
        }
    }
}
