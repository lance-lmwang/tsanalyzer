#include "tsa_clock.h"

#include <math.h>
#include <stdio.h>

#include "tsa.h"
#include "tsa_log.h"
#include "tsa_units.h"

#define TAG "CLOCK"
#define PCR_REPETITION_MAX_TICKS 1350000                   /* 50ms */
#define PCR_ARRIVAL_MAX_NS 100000000ULL                    /* 100ms */
#define PCR_SYNC_THRESHOLD_TICKS (TS_SYSTEM_CLOCK_HZ / 10) /* 100ms */
#define PCR_STABILIZE_COUNT 10

static uint64_t parse_pcr_27mhz(const uint8_t *p) {
    uint64_t base =
        ((uint64_t)p[0] << 25) | ((uint64_t)p[1] << 17) | ((uint64_t)p[2] << 9) | ((uint64_t)p[3] << 1) | (p[4] >> 7);
    uint16_t ext = ((uint16_t)(p[4] & 0x01) << 8) | p[5];
    return base * 300 + ext;
}

void tsa_clock_reset(tsa_clock_inspector_t *inspector) {
    if (!inspector) return;
    inspector->state = TSA_CLOCK_STATE_RESET;
    inspector->initialized = false;
    inspector->last_pcr_val = 0;
    inspector->last_pcr_local_ns = 0;
    inspector->pcr_count = 0;
    inspector->filtered_rate = 0.027;
    inspector->pending_discontinuity = false;
    inspector->last_error_pcr_val = 0;
    /* priority_1_errors is NOT reset here to maintain cumulative counter */
}

void tsa_clock_update(const uint8_t *packet, tsa_clock_inspector_t *inspector, uint64_t now_ns, bool is_live) {
    if (!packet || !inspector) return;

    if (!(packet[3] & 0x20)) return;
    uint8_t af_len = packet[4];
    if (af_len < 7 || !(packet[5] & 0x10)) return;

    uint64_t current_pcr = parse_pcr_27mhz(&packet[6]);
    bool has_discontinuity = (packet[5] & 0x80) != 0;

    /* 1. Reset on Sequence Break */
    if (has_discontinuity || (inspector->initialized && current_pcr < inspector->last_pcr_val)) {
        tsa_clock_reset(inspector);
    }

    if (inspector->state == TSA_CLOCK_STATE_RESET) {
        inspector->last_pcr_val = current_pcr;
        inspector->last_pcr_local_ns = now_ns;
        inspector->state = TSA_CLOCK_STATE_SYNCING;
        inspector->pcr_count = 1;
        inspector->initialized = true;
        return;
    }

    uint64_t pcr_delta = current_pcr - inspector->last_pcr_val;
    uint64_t arrival_delta_ns = (now_ns > inspector->last_pcr_local_ns) ? (now_ns - inspector->last_pcr_local_ns) : 0;

    /* 2. SYNCING State: Require 30 stable PCRs */
    if (inspector->state == TSA_CLOCK_STATE_SYNCING) {
        if (pcr_delta > PCR_SYNC_THRESHOLD_TICKS) {
            inspector->pcr_count = 1;
        } else {
            inspector->pcr_count++;
        }
        inspector->last_pcr_val = current_pcr;
        inspector->last_pcr_local_ns = now_ns;
        if (inspector->pcr_count >= PCR_STABILIZE_COUNT) {
            inspector->state = TSA_CLOCK_STATE_LOCKED;
        }
        return;
    }

    /* 3. LOCKED State: Precise Dual-Criteria Monitoring */
    bool violation = (pcr_delta > PCR_REPETITION_MAX_TICKS) || (is_live && arrival_delta_ns > PCR_ARRIVAL_MAX_NS);

    if (violation) {
        if (pcr_delta > (TS_SYSTEM_CLOCK_HZ * 5)) {
            tsa_clock_reset(inspector);
            return;
        }

        /* Atomic Capture: If this is a new gap, increment and immediately force RE-SYNC */
        if (inspector->last_error_pcr_val != inspector->last_pcr_val) {
            inspector->priority_1_errors++;
            inspector->last_error_pcr_val = inspector->last_pcr_val;

            /* Physically cut off further errors by dropping out of LOCKED state */
            inspector->state = TSA_CLOCK_STATE_SYNCING;
            inspector->pcr_count = 1;

            if (is_live) {
                tsa_error(TAG, "PID 0x%04x: PCR Violation. P-Gap: %.2f ms, A-Gap: %.2f ms. Resetting sync.",
                          inspector->pid, tsa_pcr_to_ns_f((double)pcr_delta) / 1000000.0,
                          (double)arrival_delta_ns / 1000000.0);
            } else {
                tsa_error(TAG, "PID 0x%04x: PCR Violation. P-Gap: %.2f ms. Resetting sync.", inspector->pid,
                          tsa_pcr_to_ns_f((double)pcr_delta) / 1000000.0);
            }

            /* Final safeguard: Update reference so next packet starts fresh */
            inspector->last_pcr_val = current_pcr;
            inspector->last_pcr_local_ns = now_ns;
            return;
        }
    }

    /* 4. Alpha-Beta Filter Trace */
    if (arrival_delta_ns > 0 && arrival_delta_ns < 1000000000ULL) {
        double predicted = inspector->filtered_rate * (double)arrival_delta_ns;
        double residual = (double)pcr_delta - predicted;
        inspector->filtered_rate += (0.005 / (double)arrival_delta_ns) * residual;
        double instant_jitter = tsa_pcr_to_ns_f(residual) / 1000000.0;
        inspector->pcr_jitter_ms = (inspector->pcr_jitter_ms * 0.9) + (instant_jitter * 0.1);
    }

    inspector->last_pcr_val = current_pcr;
    inspector->last_pcr_local_ns = now_ns;
    inspector->pcr_count++;
}
