#include "tss_internal.h"
#include <string.h>
#include <math.h>

void tss_account_null_packet(tsshaper_t *ctx) {
    int has_data = 0;
    int tb_full = 0;
    int no_token = 0;
    int i;

    for (i = 0; i < ctx->nb_all_pids; i++) {
        tss_pid_state_t *p = ctx->all_pids[i];
        if (tss_fifo_size(p->ts_fifo) >= TSS_TS_PACKET_SIZE) {
            has_data = 1;
            if (p->tb_fullness_bits + (int64_t)TSS_TS_PACKET_BITS > p->tb_size_bits) {
                tb_full = 1;
            } else {
                if (!p->is_panic_mode && p->tokens_bits < (int64_t)TSS_TS_PACKET_BITS) {
                    no_token = 1;
                }
            }
        }
    }

    ctx->dbg_cnt_null++;
    ctx->tel_sec_null_packets++;
    if (!has_data) ctx->dbg_null_reason_nodata++;
    else if (tb_full) ctx->dbg_null_reason_tb++;
    else if (no_token) ctx->dbg_null_reason_token++;
}

void tss_account_metrology(tsshaper_t *ctx) {
    int i;
    bool trigger = false;

    if (ctx->tel_last_1s_stc == TSS_NOPTS_VALUE) {
        ctx->tel_last_1s_stc = ctx->v_stc;
        return;
    }

    if (ctx->v_stc - ctx->tel_last_1s_stc >= TSS_SYS_CLOCK_FREQ) {
        trigger = true;
    }

    if (trigger) {
        int64_t v_in = 0, v_out = 0, v_delay_ms = 0, a_delay_ms = 0;
        int64_t v_in_ema = 0, v_slack_min_ms = 99999;
        int64_t v_in_win_avg = 0, v_in_win_max = 0, v_max_au_kb = 0;
        int64_t v_out_win_avg = 0, v_out_win_max = 0;
        int v_pkt_count = 0;
        int64_t v_ratio = 0, v_target_k = 0, v_pace = 0;
        int v_max_mode = -1;
        const char *mode_strs[] = { "LOCK", "RECO", "PREE", "URGE" };

        for (i = 0; i < ctx->nb_all_pids; i++) {
            tss_pid_state_t *p = ctx->all_pids[i];

            if (p->type == TSS_PID_VIDEO) {
                int64_t pid_out_bps_1s = (p->mux_output_bytes - p->last_1s_emitted_bytes) * 8;
                int64_t pid_in_bps_1s = (p->fifo_accept_bytes - p->last_1s_accept_bytes) * 8;
                int64_t pid_drop_bps_1s = (p->drop_bytes_total - p->last_1s_drop_bytes) * 8;
                p->last_1s_emitted_bytes = p->mux_output_bytes;
                p->last_1s_accept_bytes = p->fifo_accept_bytes;
                p->last_1s_drop_bytes = p->drop_bytes_total;

                p->last_display_out_bps = pid_out_bps_1s;
                p->last_display_in_bps = pid_in_bps_1s;
                p->last_display_drop_bps = pid_drop_bps_1s;
                p->last_display_out_pkts = (int)(pid_out_bps_1s / (TSS_TS_PACKET_BITS));

                /* 5-Sample Window */
                p->tel_in_bps_history[p->tel_in_bps_idx] = p->last_display_in_bps;
                p->tel_in_bps_idx = (p->tel_in_bps_idx + 1) % 5;
                if (p->tel_in_bps_count < 5) p->tel_in_bps_count++;

                {
                    int64_t win_sum_in = 0, win_max_in = 0;
                    for (int k = 0; k < p->tel_in_bps_count; k++) {
                        win_sum_in += p->tel_in_bps_history[k];
                        if (p->tel_in_bps_history[k] > win_max_in) win_max_in = p->tel_in_bps_history[k];
                    }
                    p->tel_in_bps_avg = win_sum_in / p->tel_in_bps_count;
                    p->tel_in_bps_max = win_max_in;
                }

                p->tel_out_bps_history[p->tel_out_bps_idx] = p->last_display_out_bps;
                p->tel_out_bps_idx = (p->tel_out_bps_idx + 1) % 5;
                if (p->tel_out_bps_count < 5) p->tel_out_bps_count++;

                {
                    int64_t win_sum_out = 0, win_max_out = 0;
                    for (int k = 0; k < p->tel_out_bps_count; k++) {
                        win_sum_out += p->tel_out_bps_history[k];
                        if (p->tel_out_bps_history[k] > win_max_out) win_max_out = p->tel_out_bps_history[k];
                    }
                    p->tel_out_bps_avg = win_sum_out / p->tel_out_bps_count;
                    p->tel_out_bps_max = win_max_out;
                }

                /* EMA Input Trend */
                if (p->bitrate_bps > 0) {
                    if (p->tel_input_ema_bps == 0) p->tel_input_ema_bps = p->bitrate_bps;
                    else {
                        int64_t ln = 10000;
                        p->tel_input_ema_bps = (p->tel_input_ema_bps * (ln - 1) + p->last_display_in_bps) / ln;
                    }
                    v_delay_ms = (p->continuous_fullness_bits * 1000) / p->bitrate_bps;
                    if (ctx->cfg.mux_delay_ms > 0)
                        v_ratio = (v_delay_ms * 100) / ctx->cfg.mux_delay_ms;
                    v_target_k = p->bitrate_bps / 1000;
                }

                v_in += p->last_display_in_bps / 8;
                v_out += p->last_display_out_bps / 8;
                v_pkt_count += p->last_display_out_pkts;
                v_pace = p->pacing_tokens;
                v_in_ema = p->tel_input_ema_bps;
                v_in_win_avg = p->tel_in_bps_avg;
                v_in_win_max = p->tel_in_bps_max;
                v_out_win_avg = p->tel_out_bps_avg;
                v_out_win_max = p->tel_out_bps_max;
                v_max_au_kb = p->tel_sec_max_au_bits / 1000;
                if (p->telemetry_mode > v_max_mode) v_max_mode = p->telemetry_mode;
                if (p->tel_sec_slack_min_27m / 27000 < v_slack_min_ms)
                    v_slack_min_ms = (p->tel_sec_slack_min_27m < 0) ? 0 : (p->tel_sec_slack_min_27m / 27000);
            } else if (p->type == TSS_PID_AUDIO) {
                if (p->bitrate_bps > 0) {
                    int64_t cur_a_delay = ((int64_t)tss_fifo_size(p->ts_fifo) * 8 * 1000) / p->bitrate_bps;
                    if (cur_a_delay > a_delay_ms) a_delay_ms = cur_a_delay;
                }
            }

            p->tel_sec_max_au_bits = 0;
            p->tel_sec_slack_min_27m = 10LL * TSS_SYS_CLOCK_FREQ;
            p->telemetry_mode = -1;
        }

        if (ctx->cfg.debug_level >= 1) {
            int null_perc = (int)(ctx->tel_sec_null_packets * 100 / ((TSS_SYS_CLOCK_FREQ / (ctx->ticks_per_packet > 0 ? ctx->ticks_per_packet : 1))));
            int64_t v_drop_k = 0;
            int64_t total_resyncs = 0;
            for (i = 0; i < ctx->nb_all_pids; i++) {
                if (ctx->all_pids[i]->type == TSS_PID_VIDEO) {
                    v_drop_k += ctx->all_pids[i]->last_display_drop_bps / 1000;
                    total_resyncs += ctx->all_pids[i]->resync_count;
                }
            }

            tss_log(ctx, TSS_LOG_INFO, "[T-STD SEC RAW] trigger reached. v_stc=%lld\n", (long long)ctx->v_stc);

            tss_log(ctx, TSS_LOG_INFO,
                    "[T-STD SEC] %3dms | In:%4dk (Avg5s:%4dk, Max5s:%4dk) | "
                    "Drop:%4dk | MaxAU:%4dk | Out:%4dk (Avg5s:%4dk, Max5s:%4dk) Pkt:%d | "
                    "Nom:%3dk | V-Dly:%4dms | A-Dly:%4dms | Slk:%dms | "
                    "VBV:%3d%% | Pace:%d.%03d | Mode:%s | Null:%2d%% | "
                    "Resync:%d | V-InEMA:%4dk\n",
                    (int)(ctx->v_stc / 27000),
                    (int)(v_in * 8 / 1000), (int)(v_in_win_avg / 1000), (int)(v_in_win_max / 1000),
                    (int)v_drop_k, (int)v_max_au_kb,
                    (int)(v_out * 8 / 1000), (int)(v_out_win_avg / 1000), (int)(v_out_win_max / 1000),
                    v_pkt_count, (int)v_target_k, (int)v_delay_ms, (int)a_delay_ms, (int)v_slack_min_ms,
                    (int)(v_ratio > 999 ? 999 : v_ratio), (int)(v_pace / 1000), (int)(v_pace % 1000),
                    (v_max_mode >= 0 && v_max_mode <= 3) ? mode_strs[v_max_mode] : "LOCK",
                    null_perc, (int)total_resyncs, (int)(v_in_ema / 1000));
        }

        ctx->tel_sec_null_packets = 0;
        ctx->tel_last_1s_stc = ctx->v_stc;
    }
}
