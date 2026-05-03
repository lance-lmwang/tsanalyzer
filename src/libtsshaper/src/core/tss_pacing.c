#include "tss_internal.h"
#include <inttypes.h>
#include <string.h>

static int64_t tss_clip64(int64_t a, int64_t amin, int64_t amax) {
    if (a < amin) return amin;
    if (a > amax) return amax;
    return a;
}

void tss_refill_tokens_flywheel(tsshaper_t *ctx)
{
    int64_t delta_27m;
    int i;

    if (ctx->last_refill_physical_stc == TSS_NOPTS_VALUE) {
        ctx->last_refill_physical_stc = ctx->physical_stc;
        return;
    }

    delta_27m = ctx->physical_stc - ctx->last_refill_physical_stc;
    if (delta_27m < 0) {
        ctx->last_refill_physical_stc = ctx->physical_stc;
        return;
    }

    if (delta_27m > 0) {
        for (i = 0; i < ctx->nb_all_pids; i++) {
            tss_pid_state_t *p = ctx->all_pids[i];
            int64_t work_bits, add_bits;
            int64_t effective_refill;

            if (!p) continue;

            /* Dynamic Refill Adjustment for Video PIDs based on Queue Delay */
            if (p->type == TSS_PID_VIDEO && p->bitrate_bps > 0) {
                int64_t anchor, multiplier, raw_target, max_slew, step, slewed_target, max_bound, min_bound;
                /* Tracking-Refill Strategy: use smoothed input EMA as physical anchor. */
                anchor = (p->tel_input_ema_bps > 0) ? p->tel_input_ema_bps : (p->bitrate_bps * 1005 / 1000);

                /* Controller Output is pacing_tokens (already clamped & slewed in scheduler).
                 * We scale the anchor to determine the raw target refill rate. */
                multiplier = (p->pacing_tokens < 100) ? TSS_PREC_SCALE : p->pacing_tokens;
                raw_target = (anchor * multiplier) / TSS_PREC_SCALE;

                /* Initialize effective rate if starting up */
                if (p->effective_refill_rate_bps == 0) {
                    p->effective_refill_rate_bps = anchor;
                }

                /* 1. Slew-Rate Limiter: Prevent sudden aggressive jumps in plant output.
                 * Max 0.5% change per step to decouple from rapid scheduler oscillation. */
                max_slew = anchor / 200;
                step = raw_target - p->effective_refill_rate_bps;
                step = tss_clip64(step, -max_slew, max_slew);
                slewed_target = p->effective_refill_rate_bps + step;

                /* 2. Low-Pass Filter (EMA): Absorb micro-fluctuations.
                 * Alpha ~ 1/16 ensures smooth response. */
                p->effective_refill_rate_bps = (p->effective_refill_rate_bps * 15 + slewed_target) / 16;

                /* 3. Hard Clamp: Bound the physical refill to +/- 20% of configuration. */
                max_bound = p->base_refill_rate_bps * 120 / 100;
                min_bound = p->base_refill_rate_bps * 80 / 100;
                p->effective_refill_rate_bps = tss_clip64(p->effective_refill_rate_bps, min_bound, max_bound);
                effective_refill = p->effective_refill_rate_bps;
            } else {
                effective_refill = p->base_refill_rate_bps;
            }

            /* Bit-level refill with micro-bit remainder to prevent truncation starvation */
            /* CRITICAL: Use the SMOOTHED effective_refill for physical token accumulation */
            work_bits = (delta_27m * effective_refill) + p->token_remainder;
            add_bits = work_bits / TSS_SYS_CLOCK_FREQ;
            p->token_remainder = work_bits % TSS_SYS_CLOCK_FREQ;

            p->tokens_bits += add_bits;
            if (p->tokens_bits > p->bucket_size_bits)
                p->tokens_bits = p->bucket_size_bits;

            /* Dynamic floor clamping to prevent permanent runaway debt
             * resulting from extreme source-pacing mismatch. */
            {
                int64_t min_floor = - (10 * (int64_t)TSS_TS_PACKET_BITS);
                if (p->tokens_bits < min_floor)
                    p->tokens_bits = min_floor;
            }
        }
        ctx->last_refill_physical_stc = ctx->physical_stc;
    }
}
