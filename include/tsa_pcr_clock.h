#ifndef TSA_PCR_CLOCK_H
#define TSA_PCR_CLOCK_H

#include <stdbool.h>
#include <stdint.h>

/**
 * TSA PCR Clock: A high-precision clock abstraction that synchronizes
 * MPEG-TS Program Clock Reference (PCR) with the System Monotonic Clock.
 */

typedef struct {
    /* Base reference: first valid PCR mapped to system time */
    uint64_t base_pcr_ticks;  // 27MHz
    uint64_t base_sys_ns;     // Monotonic NS

    /* Running state */
    uint64_t last_pcr_ticks;
    uint64_t last_sys_ns;
    uint64_t unwrapped_pcr_ticks;

    /* Drift Compensation (Exponential Moving Average) */
    double drift_ppm;   // Drift in Parts Per Million
    double rate_scale;  // 1.0 = perfect, 1.0001 = stream is fast

    bool established;
    uint32_t pcr_count;
} tsa_pcr_clock_t;

/**
 * Initializes a PCR clock instance.
 */
void tsa_pcr_clock_init(tsa_pcr_clock_t *clk);

/**
 * Updates the clock with a new PCR value and the current system time.
 * Handles 33-bit wrap-around and updates drift estimation.
 */
void tsa_pcr_clock_update(tsa_pcr_clock_t *clk, uint64_t pcr_ticks, uint64_t sys_ns);

/**
 * Converts a PCR value (27MHz) to a predicted system timestamp (nanoseconds).
 * Uses the established timebase and current drift compensation.
 */
uint64_t tsa_pcr_clock_pcr_to_sys(tsa_pcr_clock_t *clk, uint64_t pcr_ticks);

/**
 * Converts a system timestamp (nanoseconds) to a predicted PCR value.
 */
uint64_t tsa_pcr_clock_sys_to_pcr(tsa_pcr_clock_t *clk, uint64_t sys_ns);

/**
 * Resets the clock reference (useful on discontinuities).
 */
void tsa_pcr_clock_reset(tsa_pcr_clock_t *clk);

#endif /* TSA_PCR_CLOCK_H */
