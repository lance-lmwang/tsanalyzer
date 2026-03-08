#include "tsa_pcr_clock.h"

#include <math.h>
#include <string.h>

#define PCR_27MHZ_MAX_VAL \
    (1ULL << 42)  // 33-bit * 300 + 9-bit ext actually is smaller, but let's use 2^33 * 300 roughly
#define PCR_TICKS_PER_NS 0.027
#define NS_PER_PCR_TICK (1000000000ULL / 27000000.0)

void tsa_pcr_clock_init(tsa_pcr_clock_t *clk) {
    if (!clk) return;
    memset(clk, 0, sizeof(tsa_pcr_clock_t));
    clk->rate_scale = 1.0;
}

void tsa_pcr_clock_reset(tsa_pcr_clock_t *clk) {
    if (!clk) return;
    clk->established = false;
    clk->pcr_count = 0;
    clk->unwrapped_pcr_ticks = 0;
}

static uint64_t unwrap_pcr(uint64_t current, uint64_t last, uint64_t *unwrapped) {
    const uint64_t MOD = (1ULL << 33) * 300;

    if (current < last && (last - current) > (MOD / 2)) {
        /* Real Forward Wrap Detected */
        *unwrapped += (MOD - last + current);
    } else if (current < last) {
        /* Real Backward Jump (Looping) - DO NOT fake forward jump!
         * Reset unwrapped to current to signal a restart. */
        *unwrapped = current;
    } else {
        /* Normal progression */
        *unwrapped += (current - last);
    }
    return *unwrapped;
}

void tsa_pcr_clock_update(tsa_pcr_clock_t *clk, uint64_t pcr_ticks, uint64_t sys_ns) {
    if (!clk) return;

    if (!clk->established) {
        clk->base_pcr_ticks = pcr_ticks;
        clk->base_sys_ns = sys_ns;
        clk->last_pcr_ticks = pcr_ticks;
        clk->last_sys_ns = sys_ns;
        clk->unwrapped_pcr_ticks = pcr_ticks;
        clk->established = true;
        clk->pcr_count = 1;
        return;
    }

    // 1. Unwrap PCR
    unwrap_pcr(pcr_ticks, clk->last_pcr_ticks, &clk->unwrapped_pcr_ticks);

    // 2. Estimate Drift
    // Expected wall-clock time for this PCR
    uint64_t elapsed_pcr_ns = (uint64_t)((clk->unwrapped_pcr_ticks - clk->base_pcr_ticks) * NS_PER_PCR_TICK);
    uint64_t elapsed_sys_ns = sys_ns - clk->base_sys_ns;

    if (elapsed_sys_ns > 1000000000ULL) {  // Wait 1s before estimating drift
        double current_rate = (double)elapsed_pcr_ns / (double)elapsed_sys_ns;

        // Clamping: only allow drift within +/- 5000 PPM (0.5%)
        // This prevents the clock from going crazy during file reading or bursts
        if (current_rate > 0.995 && current_rate < 1.005) {
            // EMA filter for rate scale (slow adjustment)
            clk->rate_scale = (clk->rate_scale * 0.999) + (current_rate * 0.01);
            clk->drift_ppm = (clk->rate_scale - 1.0) * 1000000.0;
        }
    }

    clk->last_pcr_ticks = pcr_ticks;
    clk->last_sys_ns = sys_ns;
    clk->pcr_count++;
}

uint64_t tsa_pcr_clock_pcr_to_sys(tsa_pcr_clock_t *clk, uint64_t pcr_ticks) {
    if (!clk || !clk->established) return 0;

    // Use current unwrapped state to predict
    // We need to know if pcr_ticks has wrapped relative to base
    uint64_t p_last = clk->last_pcr_ticks;
    uint64_t u_last = clk->unwrapped_pcr_ticks;

    uint64_t unwrapped_target = u_last;
    unwrap_pcr(pcr_ticks, p_last, &unwrapped_target);

    uint64_t pcr_delta = unwrapped_target - clk->base_pcr_ticks;
    uint64_t sys_delta = (uint64_t)((double)pcr_delta * NS_PER_PCR_TICK / clk->rate_scale);

    return clk->base_sys_ns + sys_delta;
}

uint64_t tsa_pcr_clock_sys_to_pcr(tsa_pcr_clock_t *clk, uint64_t sys_ns) {
    if (!clk || !clk->established) return 0;

    uint64_t sys_delta = sys_ns - clk->base_sys_ns;
    uint64_t pcr_delta = (uint64_t)((double)sys_delta * clk->rate_scale / NS_PER_PCR_TICK);

    return (clk->base_pcr_ticks + pcr_delta) % ((1ULL << 33) * 300);
}
