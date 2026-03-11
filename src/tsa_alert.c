#include "tsa_alert.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tsa_internal.h"
#include "tsa_log.h"

static uint32_t alert_aggregator_hash(tsa_alert_id_t id, uint16_t pid) {
    uint32_t h = (uint32_t)id * 31 + pid;
    return h % TSA_ALERT_AGGREGATOR_SIZE;
}

static bool tsa_alert_should_suppress(tsa_handle_t* h, tsa_alert_id_t id, uint16_t pid, const char* name,
                                      const char* message) {
    if (h->aggregator.window_ms == 0) return false;

    uint32_t hash_idx = alert_aggregator_hash(id, pid);
    tsa_alert_aggregator_entry_t* entry = &h->aggregator.entries[hash_idx];
    uint64_t now = h->stc_ns;
    uint64_t window_ns = (uint64_t)h->aggregator.window_ms * 1000000ULL;

    if (entry->active && entry->id == id && entry->pid == pid) {
        uint64_t start = atomic_load(&entry->window_start_ns);
        if (now - start < window_ns) {
            atomic_fetch_add(&entry->hit_count, 1);
            atomic_store(&entry->last_hit_ns, now);
            return true;
        }
    }

    entry->id = id;
    entry->pid = pid;
    atomic_store(&entry->hit_count, 1);
    atomic_store(&entry->window_start_ns, now);
    atomic_store(&entry->last_hit_ns, now);
    strncpy(entry->stream_id, h->config.input_label[0] ? h->config.input_label : "unknown",
            sizeof(entry->stream_id) - 1);
    strncpy(entry->alert_name, name, sizeof(entry->alert_name) - 1);
    strncpy(entry->message, message, sizeof(entry->message) - 1);
    entry->active = true;

    return false;
}

static bool tsa_alert_is_suppressed_by_tree(tsa_handle_t* h, tsa_alert_id_t id) {
    /* Dependency Matrix:
     * 1. TS Sync Loss suppresses CC, PCR, TSTD, PAT, PMT, SDT, NIT.
     *    (Does NOT suppress PID Drop, as that indicates flow missing).
     * 2. CC Error Burst suppresses PCR Jitter (missing packets cause jitter).
     */
    if (h->alerts[TSA_ALERT_SYNC].status == TSA_ALERT_STATE_FIRING) {
        if (id == TSA_ALERT_CC || id == TSA_ALERT_PCR || id == TSA_ALERT_TSTD || id == TSA_ALERT_PAT ||
            id == TSA_ALERT_PMT || id == TSA_ALERT_SDT || id == TSA_ALERT_NIT || id == TSA_ALERT_TRANSPORT ||
            id == TSA_ALERT_CRC || id == TSA_ALERT_PTS) {
            return true;
        }
    }

    if (h->alerts[TSA_ALERT_CC].status == TSA_ALERT_STATE_FIRING) {
        if (id == TSA_ALERT_PCR || id == TSA_ALERT_TSTD) {
            return true;
        }
    }

    return false;
}

void tsa_alert_update(tsa_handle_t* h, tsa_alert_id_t id, bool has_error, const char* name, uint16_t pid) {
    tsa_alert_state_t* s = &h->alerts[id];
    uint64_t now = h->stc_ns;

    if (has_error) {
        if (tsa_alert_is_suppressed_by_tree(h, id)) {
            return;
        }

        s->last_occurrence_ns = now;
        s->count_in_window++;

        if (s->status == TSA_ALERT_STATE_OFF) {
            s->status = TSA_ALERT_STATE_FIRING;
            s->needs_resolve_msg = true;

            char msg[256];
            snprintf(msg, sizeof(msg), "CRITICAL: %s Error detected on PID %d", name, pid);

            bool suppressed = tsa_alert_should_suppress(h, id, pid, name, msg);

            if (!suppressed) {
                tsa_info("ALERT", "FIRING: %s Error detected on PID 0x%04x (stream=%s)", name, pid,
                         h->config.input_label[0] ? h->config.input_label : "unknown");

                uint64_t mask = tsa_alert_get_mask(id);
                uint64_t user_mask = h->config.alert_filter_mask;
                if (user_mask == 0) user_mask = TSA_ALERT_MASK_ALL;

                if (h->webhook && (user_mask & mask)) {
                    tsa_webhook_push_event(h->webhook, h->config.input_label, name, msg, "CRITICAL");
                }
            }
        }
    }
}

void tsa_alert_check_resolutions(tsa_handle_t* h) {
    uint64_t now_stc = h->stc_ns;
    uint64_t now_wall = h->last_snap_wall_ns;
    const uint64_t RESOLVE_TIMEOUT_NS = TSA_TR101290_PID_TIMEOUT_NS;

    /* Timer Wheel / Zero-Packet Timeout Evaluation */
    if (h->signal_lock && now_wall > h->last_packet_rx_ns) {
        if ((now_wall - h->last_packet_rx_ns) > TSA_TR101290_PAT_TIMEOUT_NS) {
            /* If no packets arrived for > 500ms, force a Sync Loss */
            tsa_alert_update(h, TSA_ALERT_SYNC, true, "SYNC", 0);
        }
    }

    if (h->signal_lock && h->stc_ns > 0) {
        if (h->last_pat_ns > 0 && (now_stc - h->last_pat_ns) > TSA_TR101290_PAT_TIMEOUT_NS)
            tsa_alert_update(h, TSA_ALERT_PAT, true, "PAT", 0);

        if (h->last_pmt_ns > 0 && (now_stc - h->last_pmt_ns) > TSA_TR101290_PMT_TIMEOUT_NS)
            tsa_alert_update(h, TSA_ALERT_PMT, true, "PMT", 0);

        if (h->last_sdt_ns > 0 && (now_stc - h->last_sdt_ns) > TSA_TR101290_SDT_TIMEOUT_NS)
            tsa_alert_update(h, TSA_ALERT_SDT, true, "SDT", 0);

        if (h->last_nit_ns > 0 && (now_stc - h->last_nit_ns) > TSA_TR101290_NIT_TIMEOUT_NS)
            tsa_alert_update(h, TSA_ALERT_NIT, true, "NIT", 0);
    }

    uint64_t agg_window_ns = (uint64_t)h->aggregator.window_ms * 1000000ULL;
    for (int i = 0; i < TSA_ALERT_AGGREGATOR_SIZE; i++) {
        tsa_alert_aggregator_entry_t* entry = &h->aggregator.entries[i];
        if (entry->active) {
            uint64_t start = atomic_load(&entry->window_start_ns);
            if (now_stc - start >= agg_window_ns) {
                uint32_t count = atomic_load(&entry->hit_count);
                if (count > 1) {
                    char summary[512];
                    snprintf(summary, sizeof(summary),
                             "[Summary] Stream %s had %u occurrences of Alert %s on PID 0x%04x in the last %u ms",
                             entry->stream_id, count, entry->alert_name, entry->pid, h->aggregator.window_ms);

                    tsa_info("ALERT", "%s", summary);

                    uint64_t mask = tsa_alert_get_mask(entry->id);
                    uint64_t user_mask = h->config.alert_filter_mask;
                    if (user_mask == 0) user_mask = TSA_ALERT_MASK_ALL;

                    if (h->webhook && (user_mask & mask)) {
                        tsa_webhook_push_event(h->webhook, entry->stream_id, entry->alert_name, summary, "SUMMARY");
                    }
                }
                entry->active = false;
            }
        }
    }

    for (int i = 0; i < TSA_ALERT_MAX; i++) {
        tsa_alert_state_t* s = &h->alerts[i];
        if (s->status == TSA_ALERT_STATE_FIRING) {
            if (now_stc - s->last_occurrence_ns > RESOLVE_TIMEOUT_NS) {
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
                    tsa_webhook_push_event(h->webhook, h->config.input_label, name, msg, "OK");
                }
                s->needs_resolve_msg = false;
                s->count_in_window = 0;
            }
        }
    }
}
