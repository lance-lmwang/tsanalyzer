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
        
        inspector->filtered_offset = 0;
        inspector->filtered_rate = 0.027; // 27MHz / 1e9 ns
        
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

    /* 6. Overall Jitter (PCR_OJ) calculation with Alpha-Beta Filter
     * This avoids the 'sawtooth' effect by estimating the actual clock rate.
     */
    int64_t dt_ns = (int64_t)(now_ns - inspector->last_pcr_local_ns);
    if (dt_ns > 0) {
        // Prediction
        double predicted_pcr_incr = (inspector->filtered_rate * dt_ns);
        
        // Measurement
        double actual_incr = (current_pcr >= inspector->last_pcr_val) ?
                               (double)(current_pcr - inspector->last_pcr_val) :
                               (double)(current_pcr + (PCR_MAX_VALUE - inspector->last_pcr_val));
        
        // Innovation (Residual)
        double residual = actual_incr - predicted_pcr_incr;
        
        // Alpha-Beta Update (tuned for stability)
        (void)0; // const double alpha = 0.05;
        const double beta = 0.005;
        
        // Update estimate
        inspector->filtered_rate += (beta / dt_ns) * residual;
        
        // Jitter is the deviation from the expected arrival
        double instant_jitter_ms = residual / PCR_TICKS_PER_MS;
        inspector->pcr_jitter_ms = (inspector->pcr_jitter_ms * 0.9) + (instant_jitter_ms * 0.1);
    }

    /* 7. Update internal state */
    inspector->last_pcr_val = current_pcr;
    inspector->last_pcr_local_ns = now_ns;
    inspector->pcr_count++;
}

void tsa_clock_reset(tsa_clock_inspector_t *inspector) {
    if (!inspector) return;
    inspector->initialized = false;
    inspector->pcr_interval_max_ticks = 0;
    inspector->pcr_jitter_ms = 0;
    inspector->pcr_count = 0;
}
