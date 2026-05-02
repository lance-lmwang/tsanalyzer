#include "tss_internal.h"
#include <string.h>
#include <stdlib.h>

#define FFABS(a) ((a) >= 0 ? (a) : (-(a)))
#define FFMAX(a,b) ((a) > (b) ? (a) : (b))
#define FFMIN(a,b) ((a) < (b) ? (a) : (b))

static int64_t tss_clip64(int64_t a, int64_t amin, int64_t amax) {
    if (a < amin) return amin;
    if (a > amax) return amax;
    return a;
}

void tss_refill_tokens_flywheel(tsshaper_t *ctx) {
    if (ctx->last_refill_physical_stc == TSS_NOPTS_VALUE) {
        ctx->last_refill_physical_stc = ctx->physical_stc;
        return;
    }
    int64_t delta = ctx->physical_stc - ctx->last_refill_physical_stc;
    if (delta <= 0) return;

    for (int i = 0; i < ctx->nb_all_pids; i++) {
        tss_pid_state_t *p = ctx->all_pids[i];
        int64_t effective_refill = p->base_refill_rate_bps;

        if (p->type == TSS_PID_VIDEO && p->bitrate_bps > 0) {
            int64_t anchor = (p->tel_input_ema_bps > 0) ? p->tel_input_ema_bps : (p->bitrate_bps * 1005 / 1000);
            int64_t multiplier = (p->pacing_tokens < 100) ? TSS_PREC_SCALE : p->pacing_tokens;
            int64_t raw_target = (anchor * multiplier) / TSS_PREC_SCALE;

            if (p->effective_refill_rate_bps == 0) p->effective_refill_rate_bps = anchor;
            int64_t max_slew = anchor / 200;
            int64_t step = raw_target - p->effective_refill_rate_bps;
            if (step > max_slew) step = max_slew;
            if (step < -max_slew) step = -max_slew;
            p->effective_refill_rate_bps = (p->effective_refill_rate_bps * 15 + (p->effective_refill_rate_bps + step)) / 16;
            effective_refill = tss_clip64(p->effective_refill_rate_bps, p->base_refill_rate_bps * 80 / 100, p->base_refill_rate_bps * 150 / 100);
        }

        int64_t work_bits = (delta * effective_refill) + p->token_remainder;
        p->tokens_bits += work_bits / TSS_SYS_CLOCK_FREQ;
        p->token_remainder = work_bits % TSS_SYS_CLOCK_FREQ;
        if (p->tokens_bits > p->bucket_size_bits) p->tokens_bits = p->bucket_size_bits;
    }
    ctx->last_refill_physical_stc = ctx->physical_stc;
}

tss_pid_state_t* tss_pick_es_pid(tsshaper_t *ctx) {
    tss_pid_state_t *best_pid = NULL;
    int64_t best_deadline = 0x7FFFFFFFFFFFFFFFLL;
    int64_t best_fullness = -1;
    bool best_is_urgent = false;

    for (int i = 0; i < ctx->nb_all_pids; i++) {
        tss_pid_state_t *pid = ctx->all_pids[i];
        if (tss_fifo_size(pid->ts_fifo) < TSS_TS_PACKET_SIZE) {
            if (pid->type == TSS_PID_VIDEO || pid->type == TSS_PID_AUDIO) {
                pid->pi_integral = 0; pid->pacing_tokens = TSS_PREC_SCALE;
            }
            continue;
        }

        /* 1:1 High-Precision Deadline Alignment */
        int64_t deadline = ctx->v_stc + (int64_t)270000;
        if (pid->au_events && tss_fifo_size(pid->au_events) >= sizeof(tss_access_unit_t)) {
            tss_access_unit_t head_au;
            if (tss_fifo_peek(pid->au_events, (uint8_t*)&head_au, sizeof(tss_access_unit_t), 0) == 0) {
                deadline = head_au.dts;
            }
        }
        pid->next_arrival_ts = deadline;

        /* PI Gain Matrix for SD/720p/1080i */
        if (pid->type == TSS_PID_VIDEO || pid->type == TSS_PID_AUDIO) {
            int64_t current_v_delay_ms = ((int64_t)tss_fifo_size(pid->ts_fifo) * 8 * 1000) / (pid->bitrate_bps > 0 ? pid->bitrate_bps : 1000000);
            int64_t delay_ratio = current_v_delay_ms * TSS_PREC_SCALE / (ctx->cfg.mux_delay_ms > 0 ? ctx->cfg.mux_delay_ms : 700);

            int64_t gain = TSS_GAIN_GENTLE;
            int64_t jitter_limit_bps = 32000;

            if (delay_ratio >= 800 && delay_ratio <= 1000) {
                gain = TSS_GAIN_ULTRA_SMOOTH; // Lockdown zone
                jitter_limit_bps = 16000;
            } else if (delay_ratio < 400 || delay_ratio > 1400) {
                gain = TSS_GAIN_EMERGENCY;
                jitter_limit_bps = pid->base_refill_rate_bps / 12;
            }

            int64_t range_limit = jitter_limit_bps * TSS_PREC_SCALE / (pid->base_refill_rate_bps > 0 ? pid->base_refill_rate_bps : 1000000);
            int64_t error = delay_ratio - TSS_RATIO_TARGET;

            if (FFABS(error) > 10) { // Sharpened deadband
                pid->pi_integral += (gain * error);
                int64_t target_pacing = pid->pacing_tokens + (pid->pi_integral / TSS_PREC_SCALE);
                pid->pi_integral %= TSS_PREC_SCALE;
                int64_t step = pid->bitrate_bps / 150000 + 5;
                pid->pacing_tokens = tss_clip64(target_pacing, pid->pacing_tokens - step, pid->pacing_tokens + step);
            }
            pid->pacing_tokens = tss_clip64(pid->pacing_tokens, TSS_PREC_SCALE - range_limit, TSS_PREC_SCALE + range_limit);
        }

        /* Token & Physical Constraints */
        if (pid->tb_fullness_bits + (int64_t)TSS_TS_PACKET_BITS > pid->tb_size_bits) continue;
        if (!pid->is_panic_mode && !ctx->in_drain && pid->tokens_bits < (int64_t)TSS_TS_PACKET_BITS) {
             if (tss_fifo_size(pid->ts_fifo) * 10 / pid->fifo_capacity < 9) continue;
        }

        /* EDF Decisions */
        int64_t slack_27m = deadline - ctx->v_stc;
        bool is_urgent = (slack_27m < (27000 * 5)) || (pid->type == TSS_PID_PSI && ctx->psi_consecutive_count < 2);

        if (pid->type == TSS_PID_AUDIO || pid->type == TSS_PID_PCR) {
            if (slack_27m < (27000 * 15)) deadline -= (27000 * 100); // Aggressive Audio Shield
        }

        bool is_better = false;
        if (is_urgent && !best_is_urgent) is_better = true;
        else if (is_urgent == best_is_urgent && deadline < best_deadline) is_better = true;
        else if (deadline == best_deadline && pid->continuous_fullness_bits > best_fullness) is_better = true;

        if (is_better) {
            best_pid = pid; best_deadline = deadline;
            best_fullness = pid->continuous_fullness_bits; best_is_urgent = is_urgent;
        }
    }
    if (best_pid && best_pid->type == TSS_PID_PSI) ctx->psi_consecutive_count++; else ctx->psi_consecutive_count = 0;
    return best_pid;
}
