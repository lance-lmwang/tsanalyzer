#include "tsa_clock.h"
#include <stdio.h>
#include <math.h>

/**
 * Parses and recombines the 33-bit PCR base and 9-bit extension.
 * Input 'p' should point directly to the 6-byte PCR field in the AF.
 */
static uint64_t parse_pcr_27mhz(const uint8_t *p) {
    uint64_t base = ((uint64_t)p[0] << 25) | ((uint64_t)p[1] << 17) | 
                    ((uint64_t)p[2] << 9) | ((uint64_t)p[3] << 1) | (p[4] >> 7);
    uint16_t ext = ((uint16_t)(p[4] & 0x01) << 8) | p[5];
    return base * 300 + ext;
}

/**
 * Calculates the nanosecond difference between two points in monotonic time.
 */
static int64_t timespec_diff_ns(const struct timespec *start, const struct timespec *end) {
    return (int64_t)(end->tv_sec - start->tv_sec) * 1000000000LL + 
           (end->tv_nsec - start->tv_nsec);
}

void tsa_clock_update(const uint8_t *packet, tsa_clock_inspector_t *inspector, uint64_t now_ns) {
    if (!packet || !inspector) return;

    /* 1. Preliminary Adaptation Field checks */
    if (!(packet[3] & 0x20)) return; /* No adaptation field */
    uint8_t af_len = packet[4];
    if (af_len < 1) return;

    /* 2. Discontinuity Handling (Mux Director feedback) */
    if (packet[5] & 0x80) {
        tsa_clock_reset(inspector);
        inspector->pending_discontinuity = true;
    }

    /* 3. Extract PCR if available */
    if (af_len < 7 || !(packet[5] & 0x10)) return; /* No PCR flag */

    uint64_t current_pcr = parse_pcr_27mhz(&packet[6]);

    /* 4. Initialization or Discontinuity recovery */
    if (!inspector->initialized) {
        inspector->first_pcr_val = current_pcr;
        inspector->first_pcr_local_ns = now_ns;
        inspector->last_pcr_val = current_pcr;
        inspector->last_pcr_local_ns = now_ns;
        inspector->initialized = true;
        inspector->pcr_count = 1;
        inspector->pending_discontinuity = false;
        return;
    }

    /* 5. PCR Interval (TR 101 290 P1.1) */
    uint64_t interval = (current_pcr >= inspector->last_pcr_val) ? 
                        (current_pcr - inspector->last_pcr_val) : 
                        (current_pcr + (PCR_MAX_VALUE - inspector->last_pcr_val));
    
    if (interval > TR101290_P1_1_THRESHOLD_TICKS) {
        inspector->priority_1_errors++;
    }
    if (interval > inspector->pcr_interval_max_ticks) {
        inspector->pcr_interval_max_ticks = interval;
    }

    /* 6. Overall Jitter (PCR_OJ) calculation */
    int64_t elapsed_ns = (int64_t)(now_ns - inspector->first_pcr_local_ns);
    int64_t expected_pcr_delta = (elapsed_ns * 27) / 1000;
    int64_t actual_pcr_delta = (current_pcr >= inspector->first_pcr_val) ?
                               (current_pcr - inspector->first_pcr_val) :
                               (current_pcr + (PCR_MAX_VALUE - inspector->first_pcr_val));

    int64_t jitter_ticks = actual_pcr_delta - expected_pcr_delta;
    inspector->pcr_jitter_ms = (double)jitter_ticks / PCR_TICKS_PER_MS;

    /* 7. Update internal state */
    inspector->last_pcr_val = current_pcr;
    inspector->last_pcr_local_ns = now_ns;
    inspector->pcr_count++;

    /* 8. Drift Compensation (every 1000 samples) */
    if (inspector->pcr_count % 1000 == 0) {
        inspector->first_pcr_val = current_pcr;
        inspector->first_pcr_local_ns = now_ns;
    }
}

void tsa_clock_reset(tsa_clock_inspector_t *inspector) {
    if (!inspector) return;
    inspector->initialized = false;
    inspector->pcr_interval_max_ticks = 0;
    inspector->pcr_jitter_ms = 0;
    inspector->pcr_count = 0;
    /* Do not reset priority_1_errors here to preserve history of the current stream session */
}
