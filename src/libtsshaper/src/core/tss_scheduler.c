#include "tss_internal.h"
#include <inttypes.h>
#include <string.h>

#define FFABS(a) ((a) >= 0 ? (a) : (-(a)))
#define FFMAX(a,b) ((a) > (b) ? (a) : (b))
#define FFMIN(a,b) ((a) < (b) ? (a) : (b))

tss_pid_state_t *tss_pick_es_pid(tsshaper_t *ctx, tss_program_t **out_prog)
{
    int i, j;
    tss_pid_state_t *best_pid = NULL;
    int64_t best_deadline = 0x7FFFFFFFFFFFFFFFLL;
    int64_t best_fullness = -1;
    int best_is_urgent = 0;

    for (i = 0; i < ctx->nb_programs; i++) {
        tss_program_t *prog = ctx->programs[i];
        for (j = 0; j < prog->nb_pids; j++) {
            tss_pid_state_t *pid = prog->pids[j];
            int64_t deadline = 0x7FFFFFFFFFFFFFFFLL;
            int64_t fullness;
            int64_t delay_ratio = 0; /* Scaled by TSS_PREC_SCALE */
            int is_urgent = 0;
            if (tss_fifo_size(pid->ts_fifo) < TSS_TS_PACKET_SIZE) {
                /* Underflow Reset: Force baseline pacing to prevent catch-up bursts after gaps */
                if (pid->type == TSS_PID_VIDEO || pid->type == TSS_PID_AUDIO) {
                    pid->pi_integral = 0;
                    pid->pacing_tokens = TSS_PREC_SCALE;
                    pid->next_pacing_stc = 0;
                }
                pid->tel_sec_wait_data++;
                continue;
            }

            /* Physical Time-Driven Gate, High-Inertia Equilibrium Model */
            if (pid->type == TSS_PID_VIDEO || pid->type == TSS_PID_AUDIO) {
                int64_t mux_delay_s_scaled = ctx->mux_delay_27m * TSS_PREC_SCALE / 27000000;
                int64_t error, gain_scaled, range_limit;
                int64_t jitter_limit_bps = 32000; /* Standard +/- 32k corridor */

                if (pid->bitrate_bps > 0 && mux_delay_s_scaled > 0) {
                    int64_t current_v_delay_ms = ((int64_t)tss_fifo_size(pid->ts_fifo) * 8 * 1000) / (pid->bitrate_bps > 0 ? pid->bitrate_bps : 1000000);
                    delay_ratio = current_v_delay_ms * TSS_PREC_SCALE / (ctx->mux_delay_27m / 27000);
                }

                /* Functional Equivalence: Dynamic Pacing Corridors */
                if (pid->tel_avg_count < 10) jitter_limit_bps = 16000;
                if (delay_ratio > TSS_RATIO_OVER_WARN) jitter_limit_bps = 64000;
                if (delay_ratio > TSS_RATIO_OVER_CRITICAL) jitter_limit_bps = (pid->base_refill_rate_bps * 20) / 100;

                /* Tiered Gain & Limit Strategy: Expanded swing range for HD smoothness */
                if (delay_ratio >= TSS_RATIO_SAFE_LOW && delay_ratio <= TSS_RATIO_SAFE_HIGH) {
                    /* Stage 1: Ultra-Smooth Zone */
                    gain_scaled = TSS_GAIN_ULTRA_SMOOTH;
                    jitter_limit_bps = 22000;
                } else if (delay_ratio >= TSS_RATIO_GENTLE_LOW && delay_ratio <= TSS_RATIO_GENTLE_HIGH) {
                    /* Stage 2: Gentle Transition */
                    gain_scaled = TSS_GAIN_GENTLE;
                    jitter_limit_bps = 32000;
                } else {
                    /* Stage 3: Emergency Correction Zone */
                    gain_scaled = TSS_GAIN_EMERGENCY;
                    /* TIERED SMOOTHING GUARD: Even in urgent mode, scale gain based on health.
                     * Provides a linear-like response curve to prevent sharp bitrate spikes. */
                    if (delay_ratio >= TSS_RATIO_OPT_LOW && delay_ratio <= TSS_RATIO_SAFE_HIGH)
                        gain_scaled = TSS_GAIN_GENTLE;
                    else if (delay_ratio >= TSS_RATIO_WARN_LOW && delay_ratio < TSS_RATIO_OPT_LOW)
                        gain_scaled = TSS_GAIN_GENTLE; /* Use same gentle gain for lower buffer to suppress spikes */
                    else if (delay_ratio >= TSS_RATIO_DANGER_LOW && delay_ratio < TSS_RATIO_WARN_LOW)
                        gain_scaled = TSS_GAIN_WARNING;

                    /* Cap jitter limit to 5% for low-bitrate streams to use buffer instead of drastic pacing change */
                    jitter_limit_bps = FFMIN(64000, pid->base_refill_rate_bps / 20);
                }

                /* Low-Bitrate Damping: Use more buffer (inertia) for SD streams (<1Mbps) */
                if (pid->base_refill_rate_bps < 1000000) {
                    gain_scaled /= 2;
                }

                range_limit = jitter_limit_bps * TSS_PREC_SCALE / (pid->base_refill_rate_bps > 0 ? pid->base_refill_rate_bps : 1000000);

                /* Pace Slew Rate Limit & Anti-Windup */
                {
                    int64_t old_pacing = pid->pacing_tokens;
                    int64_t target_pacing;
                    int64_t step;
                    int64_t max_pacing_limit = TSS_PREC_SCALE + range_limit;
                    int64_t min_pacing_limit = TSS_PREC_SCALE - range_limit;
                    int saturated = 0;

                    /* Only lift the clamp in true physical danger zones (<10% or >90%) */
                    if (delay_ratio < TSS_RATIO_DANGER_LOW || delay_ratio > TSS_RATIO_DANGER_HIGH) {
                        max_pacing_limit = (pid->base_refill_rate_bps < 1000000) ? (TSS_PREC_SCALE + 50) : (TSS_PREC_SCALE + 150);
                    }

                    error = delay_ratio - TSS_RATIO_TARGET;

                    /* Anti-Windup (Back-calculation): Stop integrating if output is saturated */
                    if (error > 0 && old_pacing >= max_pacing_limit) saturated = 1;
                    if (error < 0 && old_pacing <= min_pacing_limit) saturated = 1;

                    if (FFABS(error) < 50) {
                        target_pacing = (pid->pacing_tokens * 999999 + TSS_PREC_SCALE) / 1000000;
                        pid->pi_integral = 0;
                    } else if (FFABS(gain_scaled * error) >= TSS_PREC_SCALE) {
                        if (!saturated) {
                            pid->pi_integral += (gain_scaled * error);
                        } else {
                            /* Back-calculation equivalent: zero the integral fraction when saturated */
                            pid->pi_integral = 0;
                        }
                        target_pacing = pid->pacing_tokens + (pid->pi_integral / TSS_PREC_SCALE);
                        pid->pi_integral %= TSS_PREC_SCALE;
                    } else {
                        /* Deadband: maintain current pacing but decay slowly towards 1.0 */
                        target_pacing = (pid->pacing_tokens * 999999 + TSS_PREC_SCALE) / 1000000;
                    }

                    /* Startup Constraint Override */
                    if (pid->tel_avg_count < 10) {
                        int64_t v = target_pacing;
                        if (v < TSS_PREC_SCALE - 20) v = TSS_PREC_SCALE - 20;
                        if (v > TSS_PREC_SCALE + 20) v = TSS_PREC_SCALE + 20;
                        target_pacing = v;
                    }

                    /* Apply Slew Rate Limiting: Step size proportional to bitrate, with 10-unit floor */
                    step = FFMAX(pid->bitrate_bps / 200000, 10);
                    if (target_pacing > old_pacing + step) target_pacing = old_pacing + step;
                    if (target_pacing < old_pacing - step) target_pacing = old_pacing - step;
                    pid->pacing_tokens = target_pacing;

                    /* Hard Pacing Clamp:
                     * 1. Safe Zone (10%-90%): Symmetric clamp based on jitter_limit_bps to ensure smoothness.
                     * 2. Emergency Zone: Allow full max_pacing (1.15x) for physical safety. */
                    if (pid->pacing_tokens < min_pacing_limit) pid->pacing_tokens = min_pacing_limit;
                    if (pid->pacing_tokens > max_pacing_limit) pid->pacing_tokens = max_pacing_limit;
                }
                if (delay_ratio > TSS_RATIO_OPT_LOW) pid->telemetry_mode = 1; else pid->telemetry_mode = 0;
            }

            /* TB_n Overflow Prevention (Hard Constraint) */
            if (pid->tb_fullness_bits + (int64_t)TSS_TS_PACKET_BITS > pid->tb_size_bits)
                continue;

            pid->is_urgent = 0;
            if (pid->au_events && tss_fifo_size(pid->au_events) >= sizeof(tss_access_unit_t)) {
                tss_access_unit_t head_au;
                int64_t slack_27m;
                tss_fifo_peek(pid->au_events, (uint8_t*)&head_au, sizeof(tss_access_unit_t), 0);
                deadline = head_au.dts;

                /* --- STARTUP DIAGNOSTIC: Capture the initial time-axis alignment --- */
                if (ctx->cfg.debug_level >= 2 && pid->tel_avg_count == 0 && pid->type == TSS_PID_VIDEO) {
                    tss_log(ctx, TSS_LOG_DEBUG,
                           "[T-STD STARTUP] PID 0x%04x: First DTS=%"PRId64"ms, Initial vSTC=%"PRId64"ms, Slack=%"PRId64"ms, MuxDelay=%"PRId64"ms\n",
                           pid->pid, (deadline/27000), (ctx->v_stc/27000),
                           ((deadline - ctx->v_stc)/27000), (ctx->mux_delay_27m/27000));
                }

                /* Urgent Criteria: DTS deadline approaching (5ms margin) OR severe congestion (90% ratio) */
                slack_27m = deadline - ctx->v_stc;
                if (slack_27m < pid->tel_sec_slack_min_27m)
                    pid->tel_sec_slack_min_27m = slack_27m;

                /* A/V Alignment: Audio should also respect muxdelay to prevent "racing ahead".
                 * If the stream is audio/pcr, we only consider it urgent if its delay
                 * is comparable to the video delay goal. */
                if (pid->type == TSS_PID_AUDIO || pid->type == TSS_PID_PCR) {
                    /* If audio is 200ms ahead of its target physical delay, don't be urgent. */
                    if (slack_27m > (ctx->mux_delay_27m + TSS_SYS_CLOCK_FREQ / 5)) {
                        is_urgent = 0;
                    } else if ((slack_27m < (TSS_SYS_CLOCK_FREQ / 200LL)) || (delay_ratio > 1)) {
                        is_urgent = 1;
                        pid->is_urgent = 1;
                    }
                } else if ((slack_27m < (TSS_SYS_CLOCK_FREQ / 200LL)) || (delay_ratio > 1)) {
                    is_urgent = 1;
                    pid->is_urgent = 1;
                }

                if (pid->is_urgent) pid->telemetry_mode = 3; /* URGE */
            } else {
                /* Without AU info, assign a relaxed future deadline to prioritize valid AUs */
                deadline = ctx->v_stc + ctx->mux_delay_27m;
            }

            /* 2. Token Gate (Respecting Pacing even in Drain mode to maintain bitrate) */
            if (!pid->is_panic_mode) {
                /* In Drain mode, force Pace to 1.0 to match long-term average. */
                if (ctx->in_drain) {
                    pid->pacing_tokens = TSS_PREC_SCALE;
                }

                if (pid->tokens_bits < (int64_t)TSS_TS_PACKET_BITS) {
                    pid->tel_sec_wait_tok++;
                    continue;
                }
            }

            /* 3. Hierarchical Selection Logic */
            fullness = pid->continuous_fullness_bits;

            {
                int is_better = 0;

                /* Priority Model: SI > Urgent > Normal (EDF-based) */
                /* Bounded PSI Priority: After 2 consecutive PSI packets, yield to A/V to prevent starvation. */
                if (pid->type == TSS_PID_PSI && ctx->psi_consecutive_count < 2 && (!best_pid || best_pid->type != TSS_PID_PSI)) {
                    is_better = 1;
                } else if (best_pid && best_pid->type == TSS_PID_PSI && pid->type != TSS_PID_PSI) {
                    /* If PSI already reached its bound, it no longer wins by default.
                     * We proceed to check if A/V is urgent or has better deadline. */
                    if (ctx->psi_consecutive_count >= 2) {
                        is_better = 1;
                    } else {
                        is_better = 0;
                    }
                } else if (is_urgent && !best_is_urgent) {
                    is_better = 1;
                } else if (is_urgent == best_is_urgent) {
                    int64_t deadline_diff = (best_deadline == 0x7FFFFFFFFFFFFFFFLL) ? 0 : (deadline - best_deadline);

                    /* Apply Jitter Penalty for Audio/Clock PIDs */
                    if (pid->type == TSS_PID_AUDIO || pid->type == TSS_PID_PCR) {
                        /* Prioritize if slack is getting dangerously low */
                        if (pid->tel_sec_slack_min_27m < (TSS_SYS_CLOCK_FREQ / 100)) {
                             deadline -= (TSS_SYS_CLOCK_FREQ / 50); /* Proactive 20ms pull-forward */
                        }
                    }

                    if (deadline < best_deadline - (TSS_SYS_CLOCK_FREQ / 1000)) {
                        is_better = 1;
                    } else if (FFABS(deadline_diff) <= (TSS_SYS_CLOCK_FREQ / 1000)) {
                        if (fullness > best_fullness) {
                            is_better = 1;
                        } else if (fullness == best_fullness) {
                            if (pid == ctx->last_pid && pid->burst_count < TSS_MAX_BURST_PACKETS) is_better = 1;
                        }
                    }
                }

                if (is_better) {
                    best_deadline = deadline;
                    best_fullness = fullness;
                    best_is_urgent = is_urgent;
                    best_pid = pid;
                    if (out_prog) *out_prog = prog;
                }
            }
        }
    }
    return best_pid;
}
