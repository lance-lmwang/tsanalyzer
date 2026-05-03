#include "tss_internal.h"
#include <inttypes.h>
#include <string.h>

#define FFMAX(a,b) ((a) > (b) ? (a) : (b))

static int64_t tss_clip64(int64_t a, int64_t amin, int64_t amax) {
    if (a < amin) return amin;
    if (a > amax) return amax;
    return a;
}

void tss_account_null_packet(tsshaper_t *ctx)
{
    int has_data = 0;
    int tb_full = 0;
    int no_token = 0;
    int i;

    for (i = 0; i < ctx->nb_all_pids; i++) {
        tss_pid_state_t *p = ctx->all_pids[i];
        if (p->ts_fifo && tss_fifo_size(p->ts_fifo) >= TSS_TS_PACKET_SIZE) {
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

    if (has_data && (ctx->packet_count % 1000 == 0)) {
        if (tb_full) {
            tss_log(ctx, TSS_LOG_INFO, "[T-STD] NULL: Wait TB leak @%"PRId64"ms\n", ctx->v_stc / 27000);
        }
        if (no_token) {
            for (i = 0; i < ctx->nb_all_pids; i++) {
                tss_pid_state_t *p = ctx->all_pids[i];
                if (p->ts_fifo && tss_fifo_size(p->ts_fifo) >= TSS_TS_PACKET_SIZE) {
                    if (ctx->cfg.debug_level >= 2) {
                        tss_log(ctx, TSS_LOG_DEBUG, "[T-STD NULL] PID 0x%04x: tokens=%"PRId64", next=%"PRId64"ms, vSTC=%"PRId64"ms\n",
                               p->pid, p->tokens_bits, p->next_arrival_ts/27000, ctx->v_stc/27000);
                    }
                }
            }
        }
    }
}

void tss_print_summary(tsshaper_t *ctx, int64_t start_v_stc, int steps)
{
    if (!ctx->cfg.debug_level)
        return;

    tss_log(ctx, TSS_LOG_INFO, "[T-STD] vSTC jumped from %"PRId64"ms to %"PRId64"ms (+%"PRId64"ms). Steps taken: %d.\n",
           (start_v_stc / 27000), (ctx->v_stc / 27000),
           ((ctx->v_stc - start_v_stc) / 27000), steps);

    tss_log(ctx, TSS_LOG_INFO, "================ T-STD METRICS SUMMARY ================\n");
    tss_log(ctx, TSS_LOG_INFO, "  Total Packets Written: %"PRId64"\n",
            (ctx->dbg_cnt_payload + ctx->dbg_cnt_null + ctx->dbg_cnt_pcr + ctx->dbg_cnt_si));
    tss_log(ctx, TSS_LOG_INFO, "    - Payload (A/V)    : %"PRId64" (%.2f%%)\n",
            ctx->dbg_cnt_payload, ctx->packet_count > 0 ? (100.0 * ctx->dbg_cnt_payload / ctx->packet_count) : 0.0);
    tss_log(ctx, TSS_LOG_INFO, "    - PCR Only         : %"PRId64" (%.2f%%)\n",
            ctx->dbg_cnt_pcr, ctx->packet_count > 0 ? (100.0 * ctx->dbg_cnt_pcr / ctx->packet_count) : 0.0);
    tss_log(ctx, TSS_LOG_INFO, "    - SI (PAT/PMT/SDT) : %"PRId64" (%.2f%%)\n",
            ctx->dbg_cnt_si, ctx->packet_count > 0 ? (100.0 * ctx->dbg_cnt_si / ctx->packet_count) : 0.0);
    tss_log(ctx, TSS_LOG_INFO, "    - NULL Packets     : %"PRId64" (%.2f%%)\n",
            ctx->dbg_cnt_null, ctx->packet_count > 0 ? (100.0 * ctx->dbg_cnt_null / ctx->packet_count) : 0.0);
    tss_log(ctx, TSS_LOG_INFO, "      * Reason: TB Full: %"PRId64"\n", ctx->dbg_null_reason_tb);
    tss_log(ctx, TSS_LOG_INFO, "      * Reason: No Tokn: %"PRId64"\n", ctx->dbg_null_reason_token);
    tss_log(ctx, TSS_LOG_INFO, "      * Reason: No Data: %"PRId64"\n", ctx->dbg_null_reason_nodata);
    tss_log(ctx, TSS_LOG_INFO, "=======================================================\n");
}

void tss_account_metrology(tsshaper_t *ctx)
{
    int i;
    int trigger_metrology = 0;

    if (ctx->cfg.pcr_window_size == 0) {
        if (ctx->tel_last_1s_stc == TSS_NOPTS_VALUE) {
            ctx->tel_last_1s_stc = ctx->v_stc;
        } else if (ctx->v_stc - ctx->tel_last_1s_stc >= TSS_SYS_CLOCK_FREQ) {
            trigger_metrology = 1;
        }
    } else if (ctx->tel_trigger_metrology) {
        trigger_metrology = 1;
        ctx->tel_trigger_metrology = 0;
        ctx->tel_last_1s_stc = ctx->v_stc;
    }

    if (trigger_metrology) {
        int64_t v_in = 0, v_out = 0, v_delay_ms = 0, a_delay_ms = 0;
        int64_t v_in_ema = 0, v_slack_min_ms = 99999;
        int64_t v_in_win_avg = 0, v_in_win_max = 0, v_max_au_kb = 0;
        int64_t v_out_win_avg = 0, v_out_win_max = 0;
        int v_wait_data = 0, v_wait_tok = 0, v_pkt_count = 0;
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

                if (p->sec_num_windows > 0) {
                    p->last_display_out_bps = p->sec_sum_window_out_bps / p->sec_num_windows;
                    p->last_display_in_bps = p->sec_sum_window_in_bps / p->sec_num_windows;
                    p->last_display_drop_bps = p->sec_sum_window_drop_bps / p->sec_num_windows;
                    p->last_display_out_pkts = p->sec_sum_window_pkts / p->sec_num_windows;
                    p->sec_sum_window_out_bps = 0;
                    p->sec_sum_window_in_bps = 0;
                    p->sec_sum_window_drop_bps = 0;
                    p->sec_sum_window_pkts = 0;
                    p->sec_num_windows = 0;
                } else {
                    p->last_display_out_bps = pid_out_bps_1s;
                    p->last_display_in_bps = pid_in_bps_1s;
                    p->last_display_drop_bps = pid_drop_bps_1s;
                    p->last_display_out_pkts = (int)(pid_out_bps_1s / (TSS_TS_PACKET_SIZE * 8));
                }

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

                if (p->bitrate_bps > 0) {
                    int64_t muxdelay_ms = ctx->mux_delay_27m / 27000;
                    if (p->tel_input_ema_bps == 0) {
                        p->tel_input_ema_bps = p->bitrate_bps;
                    } else {
                        int64_t ln = 10000;
                        int64_t cur = p->tel_input_ema_bps;
                        p->tel_input_ema_bps = (cur * (ln - 1) + p->last_display_in_bps) / ln;
                    }

                    p->refill_rate_base = p->tel_input_ema_bps;

                    p->tel_avg_count++;
                    v_delay_ms = (p->continuous_fullness_bits * 1000) / p->bitrate_bps;
                    if (muxdelay_ms > 0)
                        v_ratio = (v_delay_ms * 100) / muxdelay_ms;
                    v_target_k = p->refill_rate_base / 1000;
                }

                v_in += p->last_display_in_bps / 8;
                v_out += p->last_display_out_bps / 8;
                v_pkt_count += p->last_display_out_pkts;
                if (p->telemetry_mode > v_max_mode) v_max_mode = p->telemetry_mode;
                v_pace = p->pacing_tokens;
                v_in_ema = p->tel_input_ema_bps;
                v_in_win_avg = p->tel_in_bps_avg;
                v_in_win_max = p->tel_in_bps_max;
                v_out_win_avg = p->tel_out_bps_avg;
                v_out_win_max = p->tel_out_bps_max;
                v_max_au_kb = p->tel_sec_max_au_bits / 1000;
                v_wait_data += p->tel_sec_wait_data;
                v_wait_tok += p->tel_sec_wait_tok;
                if (p->tel_sec_slack_min_27m / 27000 < v_slack_min_ms)
                    v_slack_min_ms = FFMAX(p->tel_sec_slack_min_27m / 27000, 0);
            } else if (p->type == TSS_PID_AUDIO) {
                if (p->bitrate_bps > 0) {
                    int64_t cur_a_delay = ((int64_t)tss_fifo_size(p->ts_fifo) * 8 * 1000) / (p->bitrate_bps > 0 ? p->bitrate_bps : 1000000);
                    if (cur_a_delay > a_delay_ms) a_delay_ms = cur_a_delay;
                }
            }
            p->tel_sec_wait_data = 0;
            p->tel_sec_wait_tok = 0;
            p->tel_sec_max_au_bits = 0;
            p->tel_sec_slack_min_27m = 10LL * TSS_SYS_CLOCK_FREQ;
            p->telemetry_mode = -1;
        }

        if (ctx->cfg.debug_level >= 1) {
            int64_t vbv_display = tss_clip64(v_ratio, 0, 999);
            int null_perc = (int)(ctx->tel_sec_null_packets * 100 / ((TSS_SYS_CLOCK_FREQ / (ctx->ticks_per_packet > 0
                                ? ctx->ticks_per_packet : 1)) > 0 ? (TSS_SYS_CLOCK_FREQ / (ctx->ticks_per_packet > 0
                                    ? ctx->ticks_per_packet : 1)) : 1));
            int64_t v_drop_k = 0;
            int64_t total_resyncs = 0;
            for (i = 0; i < ctx->nb_all_pids; i++) {
                if (ctx->all_pids[i]->type == TSS_PID_VIDEO) {
                    v_drop_k += ctx->all_pids[i]->last_display_drop_bps / 1000;
                    total_resyncs += ctx->all_pids[i]->resync_count;
                }
            }

            tss_log(ctx, TSS_LOG_INFO,
                    "[T-STD SEC] %3"PRId64"s | In:%4"PRId64"k (Avg5s:%4"PRId64"k, Max5s:%4"PRId64"k) | "
                    "Drop:%4"PRId64"k | MaxAU:%4"PRId64"k | Out:%4"PRId64"k (Avg5s:%4"PRId64"k, Max5s:%4"PRId64"k) Pkt:%d | "
                    "Nom:%3"PRId64"k | V-Dly:%4"PRId64"ms | A-Dly:%4"PRId64"ms | Slk:%"PRId64"ms | "
                    "VBV:%3"PRId64"%% | Pace:%"PRId64".%03"PRId64" | Mode:%s | Null:%2d%% | "
                    "V-Wait(D:%d, T:%d) | Resync:%"PRId64" | V-InEMA:%"PRId64"k\n",
                    (int64_t)(ctx->v_stc / TSS_SYS_CLOCK_FREQ),
                    (int64_t)(v_in * 8 / 1000),
                    (int64_t)(v_in_win_avg / 1000),
                    (int64_t)(v_in_win_max / 1000),
                    v_drop_k,
                    (int64_t)v_max_au_kb,
                    (int64_t)(v_out * 8 / 1000),
                    (int64_t)(v_out_win_avg / 1000),
                    (int64_t)(v_out_win_max / 1000),
                    v_pkt_count,
                    v_target_k,
                    v_delay_ms,
                    a_delay_ms,
                    v_slack_min_ms,
                    vbv_display,
                    v_pace / 1000, v_pace % 1000,
                    (v_max_mode >= 0 && v_max_mode <= 3) ? mode_strs[v_max_mode] : "LOCK",
                    null_perc,
                    v_wait_data, v_wait_tok,
                    total_resyncs,
                    (int64_t)(v_in_ema / 1000));

            ctx->tel_sec_null_packets = 0;
        }

        ctx->tel_last_1s_stc = ctx->v_stc;
    }
}
