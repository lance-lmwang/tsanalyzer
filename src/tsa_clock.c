#include "tsa_clock.h"

#include <math.h>
#include <stdio.h>

#include "tsa_log.h"

#define TAG "CLOCK"
#define PCR_REPETITION_MAX_TICKS 1080000 /* 40ms */
/* Industrial Grade: Use a much tighter burst threshold (100us) to avoid missing real recovery gaps */
#define PCR_BURST_THRESHOLD_NS 100000ULL

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
    inspector->last_error_pcr_val = 0;
}

void tsa_clock_update(const uint8_t *packet, tsa_clock_inspector_t *inspector, uint64_t now_ns) {
    if (!packet || !inspector) return;

    if (!(packet[3] & 0x20)) return;
    uint8_t af_len = packet[4];
    if (af_len < 7 || !(packet[5] & 0x10)) return;

    uint64_t current_pcr = parse_pcr_27mhz(&packet[6]);
    bool has_discontinuity = (packet[5] & 0x80) != 0;

    /* 1. Reset on Backward Jump or Hardware flag */
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

    /* 2. SYNCING Window: Require stable PCRs before allowing errors */
    if (inspector->state == TSA_CLOCK_STATE_SYNCING) {
        inspector->last_pcr_val = current_pcr;
        inspector->last_pcr_local_ns = now_ns;
        if (++inspector->pcr_count >= 10) {
            inspector->state = TSA_CLOCK_STATE_LOCKED;
        }
        return;
    }

    /* 3. LOCKED state: Deterministic monitoring */
    if (pcr_delta > PCR_REPETITION_MAX_TICKS) {
        /* Filter ultra-fast hardware bursts, but capture real OS-level disruptions */
        if (arrival_delta_ns < PCR_BURST_THRESHOLD_NS) {
            inspector->last_pcr_val = current_pcr;
            inspector->last_pcr_local_ns = now_ns;
            return;
        }

        if (pcr_delta > (PCR_27MHZ_HZ * 5)) {
            tsa_clock_reset(inspector);
            return;
        }

        if (inspector->last_error_pcr_val != inspector->last_pcr_val) {
            inspector->priority_1_errors++;
            inspector->last_error_pcr_val = inspector->last_pcr_val;
            tsa_error(TAG, "PID 0x%04x: PCR Repetition Violation! Gap: %.2f ms, Arrival: %.2f ms", inspector->pid,
                      (double)pcr_delta / 27000.0, (double)arrival_delta_ns / 1000000.0);
        }
    }

    /* 4. Alpha-Beta Jitter */
    if (arrival_delta_ns > 0 && arrival_delta_ns < 1000000000ULL) {
        double predicted = inspector->filtered_rate * (double)arrival_delta_ns;
        double residual = (double)pcr_delta - predicted;
        inspector->filtered_rate += (0.005 / (double)arrival_delta_ns) * residual;
        double instant_jitter = residual / 27000.0;
        inspector->pcr_jitter_ms = (inspector->pcr_jitter_ms * 0.9) + (instant_jitter * 0.1);
    }

    inspector->last_pcr_val = current_pcr;
    inspector->last_pcr_local_ns = now_ns;
    inspector->pcr_count++;
}
