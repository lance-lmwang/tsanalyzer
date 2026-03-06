#ifndef TSA_CLOCK_H
#define TSA_CLOCK_H

#include <stdint.h>
#include <stdbool.h>
#include <time.h>

/**
 * Constants for 27MHz PCR (Program Clock Reference) calculations.
 * PCR = (PCR_base * 300 + PCR_ext)
 */
#define PCR_27MHZ_HZ 27000000ULL
#define PCR_MAX_VALUE ((1ULL << 42) - 1) /* 33-bit base * 300 + 9-bit extension */
#define PCR_TICKS_PER_MS (PCR_27MHZ_HZ / 1000)

/**
 * TR 101 290 P1.1 (PCR_error) threshold:
 * The repetition interval of PCR shall be less than or equal to 40ms.
 * 40ms = 1,080,000 ticks at 27MHz.
 */
#define TR101290_P1_1_THRESHOLD_TICKS 1080000

typedef struct {
    uint32_t pid;
    bool initialized;

    /* Last observed PCR state (27MHz scale) */
    uint64_t last_pcr_val;          /* Raw PCR value (27MHz) from packet */
    uint64_t last_pcr_local_ns;     /* Local timestamp (ns) when last PCR was processed */

    /* Baseline reference for Overall Jitter (PCR_OJ) calculation */
    uint64_t first_pcr_val;         /* Baseline PCR value */
    uint64_t first_pcr_local_ns;    /* Baseline local timestamp (ns) */

    /* TR 101 290 Metrics */
    uint64_t pcr_interval_max_ticks; /* Maximum observed PCR repetition interval */
    double pcr_jitter_ms;            /* PCR Overall Jitter (PCR_OJ) in milliseconds */

    /* Alpha-Beta Filter for Drift Estimation
     * filtered_offset: estimated ticks offset
     * filtered_rate: estimated ticks per nanosecond (nominal is 0.027)
     */
    double filtered_offset;
    double filtered_rate;

    /* Statistics */
    uint64_t pcr_count;
    uint32_t priority_1_errors;
      /* TR 101 290 1.1 (PCR_error) repetition violation count */

    /* Discontinuity handling */
    bool pending_discontinuity;      /* Set if a discontinuity was detected, waiting for next PCR */
} tsa_clock_inspector_t;

/**
 * Extracts and updates PCR state from a TS packet.
 * Handles discontinuity flags and PCR repetition interval checks.
 *
 * @param packet 188-byte TS packet buffer.
 * @param inspector Pointer to the clock inspector instance for this PID.
 * @param now_ns Current local timestamp in nanoseconds.
 */
void tsa_clock_update(const uint8_t *packet, tsa_clock_inspector_t *inspector, uint64_t now_ns);

/**
 * Resets the inspector state. Useful when a stream changes or
 * a discontinuity is explicitly signalled.
 */
void tsa_clock_reset(tsa_clock_inspector_t *inspector);

#endif /* TSA_CLOCK_H */
