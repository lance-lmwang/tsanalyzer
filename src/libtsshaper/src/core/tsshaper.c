#include "tss_internal.h"
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

#define FFMAX(a,b) ((a) > (b) ? (a) : (b))
#define FFMIN(a,b) ((a) < (b) ? (a) : (b))

static int64_t rescale(int64_t a, int64_t b, int64_t c) {
    return (a * b) / c;
}

void tss_log(tsshaper_t *ctx, tss_log_level_t level, const char *format, ...) {
    if (ctx) {
        va_list args;
        va_start(args, format);
        char buf[1024];
        vsnprintf(buf, sizeof(buf), format, args);
        if (level <= TSS_LOG_WARN || ctx->cfg.debug_level >= 1) {
            fprintf(stderr, "%s", buf);
        }
        if (ctx->cfg.log_cb) {
            ctx->cfg.log_cb(ctx->cfg.log_opaque, level, "%s", buf);
        }
        va_end(args);
    }
}

static void insert_null_packet(tsshaper_t *ctx)
{
    uint8_t pkt[TSS_TS_PACKET_SIZE];
    memset(pkt, 0xff, TSS_TS_PACKET_SIZE);
    pkt[0] = 0x47;
    pkt[1] = 0x1f;
    pkt[2] = 0xff;
    pkt[3] = 0x10;
    if (ctx->cfg.write_cb) ctx->cfg.write_cb(ctx->cfg.write_opaque, pkt, TSS_TS_PACKET_SIZE);
}

static void inject_pcr(uint8_t *pkt, int64_t pcr)
{
    int64_t pcr_base = (pcr / 300) & 0x1FFFFFFFFLL;
    int pcr_ext = pcr % 300;
    pkt[6] = pcr_base >> 25;
    pkt[7] = pcr_base >> 17;
    pkt[8] = pcr_base >> 9;
    pkt[9] = pcr_base >> 1;
    pkt[10] = ((pcr_base & 1) << 7) | 0x7e | (pcr_ext >> 8);
    pkt[11] = pcr_ext;
}

void tsshaper_enqueue_dts(tsshaper_t *ctx, uint16_t pid_num, int64_t dts_27m, bool is_key)
{
    tss_pid_state_t *pid = tss_get_state(ctx, pid_num);
    int64_t input_dts_27m;
    int64_t rel_dts_27m;
    int i, is_master = 0;
    const char *state_names[] = {"NORMAL", "SOFT", "CONFIRMING", "HARD", "RECOVERY", "WAIT_IDR"};

    if (!ctx || !pid || dts_27m == TSS_NOPTS_VALUE)
        return;

    pid->is_current_au_key = is_key;

    if (pid->state == TSS_STATE_WAIT_IDR) {
        if (is_key || pid->type != TSS_PID_VIDEO) {
            tss_log(ctx, TSS_LOG_INFO, "[T-STD] PID 0x%04x: IDR/Key found. Resuming from Hard Resync.\n", pid->pid);
            pid->state = TSS_STATE_NORMAL;
            pid->wait_idr_start_stc = 0;
            pid->resync_count++;
            ctx->dts_epoch_invalid = 1;
            pid->need_resync = 1;
        } else {
            return;
        }
    }

    input_dts_27m = dts_27m; /* natively it did dts * 300 here, we assume dts_27m is already in 27M */

    for (i = 0; i < ctx->nb_programs; i++) {
        if (pid->pid == ctx->programs[i]->pcr_pid) {
            is_master = 1;
            break;
        }
    }

    if (ctx->first_dts == TSS_NOPTS_VALUE || (ctx->dts_epoch_invalid && is_key)) {
        ctx->first_dts = input_dts_27m;
        if (ctx->v_stc == TSS_NOPTS_VALUE) {
            ctx->dts_offset = input_dts_27m - (ctx->mux_delay_27m * 9 / 10);
            ctx->stc.base = (ctx->mux_delay_27m * 9 / 10) - (400LL * 27000);
            ctx->stc.rem = 0;
            ctx->v_stc = ctx->stc.base;
            ctx->physical_stc = ctx->v_stc;
            ctx->next_slot_stc = ctx->v_stc;
            ctx->stc_offset = ctx->v_stc;
            ctx->max_dts_seen = ctx->mux_delay_27m * 9 / 10;
            tss_log(ctx, TSS_LOG_INFO, "[T-STD] Init Unified Timeline: vSTC=%"PRId64"ms, Target_Slack=%"PRId64"ms (Sync Guard Active)\n",
                   (ctx->v_stc / 27000), (ctx->max_dts_seen / 27000));
        } else {
            int64_t target_slack = ctx->mux_delay_27m * 8 / 10;
            ctx->dts_offset = input_dts_27m - (ctx->v_stc + target_slack);
            ctx->max_dts_seen = ctx->v_stc + target_slack;
            ctx->dts_epoch_invalid = 0;
            ctx->jump_occurred = 1;
            ctx->pending_discontinuity = 1;

            for (i = 0; i < ctx->nb_all_pids; i++) {
                tss_pid_state_t *p = ctx->all_pids[i];
                if (p->ts_fifo) tss_fifo_drain(p->ts_fifo, tss_fifo_size(p->ts_fifo));
                if (p->au_events) tss_fifo_reset(p->au_events);
                p->buffer_level_bits = 0;
                p->continuous_fullness_bits = 0;
                p->tb_fullness_bits = 0;
            }

            tss_log(ctx, TSS_LOG_WARN, "[T-STD] TIMELINE JUMP DETECTED (Hard Resync): vSTC=%"PRId64"ms. RE-ANCHORING timeline.\n",
                   ctx->v_stc / 27000);
        }
        ctx->total_bytes_written = 0;
        ctx->tel_sec_start_stc = ctx->v_stc;
        ctx->last_refill_physical_stc = ctx->v_stc;
        for (i = 0; i < ctx->nb_all_pids; i++) {
            tss_pid_state_t *p = ctx->all_pids[i];
            p->last_refill_physical_stc = ctx->v_stc;
            p->last_1s_rx_bytes = 0;
            p->last_1s_accept_bytes = 0;
            p->last_1s_emitted_bytes = 0;
            p->last_1s_drop_bytes = 0;
            if (p->bitrate_bps > 0) {
                p->tokens_bits = (p->bitrate_bps * TSS_REANCHOR_TOKEN_MS) / 1000;
            } else {
                p->tokens_bits = TSS_TS_PACKET_BITS * 20;
            }
            p->need_resync = 1;
        }
    } else if (ctx->dts_epoch_invalid) {
        return;
    }

    if (pid->last_dts_raw == TSS_NOPTS_VALUE || pid->need_resync) {
        pid->next_arrival_ts = input_dts_27m - ctx->dts_offset;
        pid->last_dts_raw = input_dts_27m;
        pid->need_resync = 0;
        if (ctx->cfg.debug_level >= 1) {
            tss_log(ctx, TSS_LOG_INFO, "[T-STD] PID 0x%04x Unified First/Resynced Packet: rel_dts=%"PRId64"ms\n",
                   pid->pid, pid->next_arrival_ts / 27000);
        }
    } else {
        if (tss_voter_process(ctx, pid, input_dts_27m, is_master)) return;
        pid->next_arrival_ts = input_dts_27m - ctx->dts_offset - pid->stream_offset;
    }

    pid->rx_bytes_total += TSS_TS_PACKET_SIZE;

    rel_dts_27m = input_dts_27m - ctx->dts_offset;

    if (rel_dts_27m < ctx->v_stc + (2LL * 27000)) {
        if (ctx->v_stc != TSS_NOPTS_VALUE) {
            rel_dts_27m = ctx->v_stc + (2LL * 27000);
            pid->is_urgent = 1;
        }
    }

    if (rel_dts_27m < ctx->v_stc - TSS_LATE_THRESHOLD) {
        if (ctx->cfg.debug_level) {
            tss_log(ctx, TSS_LOG_INFO, "[T-STD] LATE CLAMP: PID=%d RelDTS=%"PRId64"ms STC=%"PRId64"ms\n",
                   pid->pid, rel_dts_27m / 27000, ctx->v_stc / 27000);        }
        rel_dts_27m = ctx->v_stc - TSS_LATE_MARGIN;
    }

    if (rel_dts_27m > ctx->max_dts_seen && (is_master || ctx->packet_count < 10000 || ctx->max_dts_seen == 0))
        ctx->max_dts_seen = rel_dts_27m;

    if (ctx->max_dts_seen > 0 && rel_dts_27m < ctx->max_dts_seen - 5LL * TSS_SYS_CLOCK_FREQ) {
        if (ctx->packet_count % 500 == 0) {
            tss_log(ctx, TSS_LOG_WARN, "[T-STD INPUT] PID 0x%04x lagging %"PRId64"s behind master. Source interleaving may be broken.\n",
                    pid->pid, (ctx->max_dts_seen - rel_dts_27m) / 27000000);
        }
    }

    if (pid->last_update_ts == TSS_NOPTS_VALUE) {
        pid->last_update_ts = (ctx->v_stc != TSS_NOPTS_VALUE) ? ctx->v_stc : 0;
    }

    if (pid->state != TSS_STATE_CONFIRMING) {
        pid->last_dts_raw = input_dts_27m;
    }

    if (pid->state != TSS_STATE_NORMAL && ctx->cfg.debug_level >= 1) {
        tss_log(ctx, TSS_LOG_INFO, "[T-STD] LOG: PID=%d rel=%"PRId64"ms STATE=%s\n",
               pid->pid, rel_dts_27m / 27000, state_names[pid->state]);
    }
}

void tsshaper_enqueue_ts(tsshaper_t* ctx, uint16_t pid_num, const uint8_t* pkt) {
    if (!ctx) return;
    tss_pid_state_t *pid = ctx->pid_map[pid_num];
    tss_internal_enqueue_packet(ctx, pid, pkt);
}

static int tss_step_internal(tsshaper_t *ctx) {
    uint8_t pkt[TSS_TS_PACKET_SIZE];
    tss_program_t *prog = NULL;
    tss_program_t *es_prog = NULL;
    tss_pid_state_t *pid = NULL;
    tss_pid_state_t *es_pid = NULL;
    int i, useful = 0, action = ACT_NULL, es_is_congested = 0;
    int64_t pcr_ideal, last_physical_for_leak;
    int64_t pcr;
    int64_t next_v_base, next_v_rem;
    int64_t next_physical_stc;

    if (!ctx) return 0;

    last_physical_for_leak = ctx->physical_stc;

    /* 1. Pre-calculate Potential Clock Advancement */
    if (ctx->stc.base == TSS_NOPTS_VALUE) {
        next_v_base = ctx->stc_offset;
        next_v_rem  = 0;
    } else {
        next_v_base = ctx->stc.base + ctx->ticks_per_packet;
        next_v_rem  = ctx->stc.rem  + ctx->rem_per_packet;
        if (next_v_rem >= ctx->stc.den) {
            next_v_base++;
            next_v_rem -= ctx->stc.den;
        }
    }

    if (ctx->physical_stc == TSS_NOPTS_VALUE) {
        next_physical_stc = ctx->stc_offset;
    } else {
        next_physical_stc = ctx->physical_stc + ctx->ticks_per_packet;
    }

    /* 2. Global Time-Driven Gate: ONLY proceed if we have reached the next physical slot */
    if (next_physical_stc < ctx->next_slot_stc)
        return 0;

    /* Gate Passed: Commit logical clock advancement */
    ctx->stc.base = next_v_base;
    ctx->stc.rem  = next_v_rem;
    ctx->v_stc    = ctx->stc.base;

    /* 3. High-Precision PCR Pulse (Calculated in physical domain) */
    {
        int64_t pcr_ticks = (11LL * 8 * TSS_SYS_CLOCK_FREQ) / ctx->mux_rate;
        pcr_ideal = ctx->physical_stc + pcr_ticks;
    }

    /* 4. Global Model Updates (Transport Buffer Leak Only - Driven by Physical Clock) */
    if (last_physical_for_leak != TSS_NOPTS_VALUE) {
        int64_t delta = next_physical_stc - last_physical_for_leak;
        if (delta > 0) {
            for (i = 0; i < ctx->nb_all_pids; i++) {
                tss_pid_state_t *p = ctx->all_pids[i];
                int64_t work_bits;
                if (!p) continue;
                work_bits = (delta * p->rx_rate_bps) + p->tb_leak_remainder;
                p->tb_leak_remainder = work_bits % TSS_SYS_CLOCK_FREQ;
                p->tb_fullness_bits = FFMAX(0, p->tb_fullness_bits - (work_bits / TSS_SYS_CLOCK_FREQ));
            }
        }
    }

    /* 6. Hierarchical Scheduler (Priority Decision Tree) */
    {
        es_pid = tss_pick_es_pid(ctx, &es_prog);
        if (es_pid && es_pid->bitrate_bps > 0) {
            int64_t safe_bitrate = FFMAX(es_pid->bitrate_bps, 1000);
            int64_t q_delay_ms = ((int64_t)tss_fifo_size(es_pid->ts_fifo) * 8 * 1000) / safe_bitrate;
            if (q_delay_ms > 1000)
                es_is_congested = 1;
        }

        /* L0: PCR Priority */
        for (i = 0; i < ctx->nb_programs; i++) {
            tss_program_t *p = ctx->programs[i];
            if (ctx->packet_count >= p->next_pcr_packet) {
                prog = p;
                pid = p->pcr_pid_state;
                action = ACT_PCR_ONLY;
                break;
            }
        }

        /* L1: Emergency Congestion Preemption (Protect TB_n) */
        if (action == ACT_PCR_ONLY && es_is_congested) {
            if (ctx->packet_count < prog->next_pcr_packet + 2) {
                pid = es_pid;
                prog = es_prog;
                action = ACT_PAYLOAD;
            }
        }

        /* L2: SI/PSI Preemption (Bounded by psi_consecutive_count) */
        if (action == ACT_NULL && ctx->psi_consecutive_count < 2) {
            if (ctx->pat_pid_state && tss_fifo_size(ctx->pat_pid_state->ts_fifo) >= TSS_TS_PACKET_SIZE) {
                pid = ctx->pat_pid_state;
                action = ACT_PAYLOAD;
            } else if (ctx->sdt_pid_state && tss_fifo_size(ctx->sdt_pid_state->ts_fifo) >= TSS_TS_PACKET_SIZE) {
                pid = ctx->sdt_pid_state;
                action = ACT_PAYLOAD;
            } else {
                for (i = 0; i < ctx->nb_programs; i++) {
                    tss_pid_state_t *p = ctx->all_pids[2 + i];
                    if (p && tss_fifo_size(p->ts_fifo) >= TSS_TS_PACKET_SIZE) {
                        pid = p;
                        action = ACT_PAYLOAD;
                        break;
                    }
                }
            }
        }

        /* L3: Normal ES Scheduling */
        if (action == ACT_NULL && es_pid) {
            pid = es_pid;
            prog = es_prog;
            action = ACT_PAYLOAD;
        }
    }

    /* 7. Physical Emission Path */
    if (action == ACT_NULL) {
        tss_pid_state_t *smooth_fallback = NULL;
        for (i = 0; i < ctx->nb_all_pids; i++) {
            tss_pid_state_t *p = ctx->all_pids[i];
            if (p && p->type == TSS_PID_VIDEO && tss_fifo_size(p->ts_fifo) >= TSS_TS_PACKET_SIZE) {
                int64_t ratio = 0;
                int can_preempt, is_critical;

                if (p->bitrate_bps > 0) {
                    int64_t safe_bitrate = FFMAX(p->bitrate_bps, 1000);
                    int64_t q_delay_ms = ((int64_t)tss_fifo_size(p->ts_fifo) * 8 * 1000) / safe_bitrate;
                    int64_t mux_floor_ms = ctx->mux_delay_27m * 12 / 10 / 27000;
                    if (mux_floor_ms > 0)
                        ratio = q_delay_ms * 1000 / mux_floor_ms;
                }

                can_preempt = (ratio > 1100 && p->tokens_bits >= (int64_t)TSS_TS_PACKET_BITS);
                is_critical = (p->is_urgent && p->tokens_bits >= (int64_t)TSS_TS_PACKET_BITS);

                if (can_preempt || is_critical) {
                    smooth_fallback = p;
                    if (p->telemetry_mode < 2) p->telemetry_mode = 2; /* PREE */
                    break;
                }
            }
        }

        if (smooth_fallback) {
            pid = smooth_fallback;
            action = ACT_PAYLOAD;
            for (i = 0; i < ctx->nb_programs; i++) {
                for (int k = 0; k < ctx->programs[i]->nb_pids; k++) {
                    if (ctx->programs[i]->pids[k] == pid) {
                        prog = ctx->programs[i];
                        break;
                    }
                }
                if (prog) break;
            }
        }
    }

    if (action == ACT_NULL) {
        int64_t v_delay_bits = 0, a_delay_bits = 0;
        if (ctx->in_drain) {
            goto post_emission;
        }

        for (i = 0; i < ctx->nb_all_pids; i++) {
            tss_pid_state_t *p = ctx->all_pids[i];
            if (p->type == TSS_PID_VIDEO) v_delay_bits += tss_fifo_size(p->ts_fifo);
            else if (p->type == TSS_PID_AUDIO) a_delay_bits += tss_fifo_size(p->ts_fifo);
        }
        if (v_delay_bits > ctx->max_fifo_size * 8 / 10 && a_delay_bits > 1024 * 1024 * 8 / 10) {
            if (ctx->packet_count % 500 == 0) {
                tss_log(ctx, TSS_LOG_ERROR, "[T-STD] Fatal backlog! Video:%"PRId64", Audio:%"PRId64" bytes. Muxrate is likely insufficient.\n",
                       (v_delay_bits/8), (a_delay_bits/8));
            }
        }

        if (ctx->cfg.debug_level)
            tss_account_null_packet(ctx);
        ctx->psi_consecutive_count = 0;
        insert_null_packet(ctx);
    } else if (action == ACT_PCR_ONLY) {
        memset(pkt, 0xff, TSS_TS_PACKET_SIZE);
        pkt[0] = 0x47;
        pkt[1] = (pid->pid >> 8) & 0x1f;
        pkt[2] = pid->pid & 0xff;
        pkt[3] = 0x20 | (pid->cc & 0x0f);
        pkt[4] = 183;
        pkt[5] = 0x10;

        pcr = pcr_ideal;
        if (ctx->pending_discontinuity) {
            pkt[5] |= 0x80;
            ctx->pending_discontinuity = 0;
            prog->last_pcr_val = TSS_NOPTS_VALUE;
        }
        if (prog->last_pcr_val != TSS_NOPTS_VALUE && pcr <= prog->last_pcr_val)
            pcr = prog->last_pcr_val + 1;
        prog->last_pcr_val = pcr;
        inject_pcr(pkt, pcr);
        prog->next_pcr_packet += prog->pcr_packet_period;

        if (prog->pcr_window_start_pcr == TSS_NOPTS_VALUE) {
            prog->pcr_window_start_pcr = pcr;
            for (int j = 0; j < prog->nb_pids; j++) {
                if (prog->pids[j]->type == TSS_PID_VIDEO) {
                    prog->pids[j]->window_start_emitted_bytes = prog->pids[j]->mux_output_bytes;
                    prog->pids[j]->window_start_rx_bytes = prog->pids[j]->rx_bytes_total;
                    prog->pids[j]->window_start_accept_bytes = prog->pids[j]->fifo_accept_bytes;
                    prog->pids[j]->window_start_drop_bytes = prog->pids[j]->drop_bytes_total;
                }
            }
        }
        prog->pcr_count++;
        if (ctx->cfg.pcr_window_size > 0 && prog->pcr_count >= ctx->cfg.pcr_window_size) {
            int64_t win_duration = pcr - prog->pcr_window_start_pcr;
            if (win_duration > 0) {
                for (int j = 0; j < prog->nb_pids; j++) {
                    tss_pid_state_t *p_win = prog->pids[j];
                    if (p_win->type == TSS_PID_VIDEO) {
                        int64_t win_out_bytes = p_win->mux_output_bytes - p_win->window_start_emitted_bytes;
                        int64_t win_in_bytes = p_win->fifo_accept_bytes - p_win->window_start_accept_bytes;
                        int64_t win_drop_bytes = p_win->drop_bytes_total - p_win->window_start_drop_bytes;
                        p_win->sec_sum_window_out_bps += rescale(win_out_bytes * 8, TSS_SYS_CLOCK_FREQ, win_duration);
                        p_win->sec_sum_window_in_bps += rescale(win_in_bytes * 8, TSS_SYS_CLOCK_FREQ, win_duration);
                        p_win->sec_sum_window_drop_bps += rescale(win_drop_bytes * 8, TSS_SYS_CLOCK_FREQ, win_duration);
                        p_win->sec_sum_window_pkts += win_out_bytes / TSS_TS_PACKET_SIZE;
                        p_win->sec_num_windows++;
                        p_win->window_start_emitted_bytes = p_win->mux_output_bytes;
                        p_win->window_start_rx_bytes = p_win->rx_bytes_total;
                        p_win->window_start_accept_bytes = p_win->fifo_accept_bytes;
                        p_win->window_start_drop_bytes = p_win->drop_bytes_total;
                    }
                }
            }
            prog->pcr_window_start_pcr = pcr;
            prog->pcr_count = 0;
            ctx->tel_trigger_metrology = 1;
        }

        if (ctx->cfg.write_cb) ctx->cfg.write_cb(ctx->cfg.write_opaque, pkt, TSS_TS_PACKET_SIZE);

        ctx->dbg_cnt_pcr++;
        if (pid->type == TSS_PID_PSI) {
            ctx->psi_consecutive_count++;
        } else {
            ctx->psi_consecutive_count = 0;
        }
        pid->mux_output_bytes += TSS_TS_PACKET_SIZE;
        pid->tb_fullness_bits += (int64_t)TSS_TS_PACKET_BITS;
        pid->tokens_bits -= (int64_t)TSS_TS_PACKET_BITS;
        useful = 1;
    } else {
        tss_fifo_read(pid->ts_fifo, pkt, TSS_TS_PACKET_SIZE);
        if (pid) pid->cc = pkt[3] & 0x0f;

        if (action == ACT_PAYLOAD_PCR) {
            pcr = pcr_ideal;
            if (ctx->pending_discontinuity) {
                pkt[5] |= 0x80;
                ctx->pending_discontinuity = 0;
            }
            prog->last_pcr_val = pcr;
            inject_pcr(pkt, pcr);
            prog->next_pcr_packet += prog->pcr_packet_period;

            if (prog->pcr_window_start_pcr == TSS_NOPTS_VALUE) {
                prog->pcr_window_start_pcr = pcr;
                for (int j = 0; j < prog->nb_pids; j++) {
                    if (prog->pids[j]->type == TSS_PID_VIDEO) {
                        prog->pids[j]->window_start_emitted_bytes = prog->pids[j]->mux_output_bytes;
                        prog->pids[j]->window_start_rx_bytes = prog->pids[j]->rx_bytes_total;
                        prog->pids[j]->window_start_accept_bytes = prog->pids[j]->fifo_accept_bytes;
                        prog->pids[j]->window_start_drop_bytes = prog->pids[j]->drop_bytes_total;
                    }
                }
            }
            prog->pcr_count++;
            if (ctx->cfg.pcr_window_size > 0 && prog->pcr_count >= ctx->cfg.pcr_window_size) {
                int64_t win_duration = pcr - prog->pcr_window_start_pcr;
                if (win_duration > 0) {
                    for (int j = 0; j < prog->nb_pids; j++) {
                        tss_pid_state_t *p_win = prog->pids[j];
                        if (p_win->type == TSS_PID_VIDEO) {
                            int64_t win_out_bytes = p_win->mux_output_bytes - p_win->window_start_emitted_bytes;
                            int64_t win_in_bytes = p_win->fifo_accept_bytes - p_win->window_start_accept_bytes;
                            int64_t win_drop_bytes = p_win->drop_bytes_total - p_win->window_start_drop_bytes;
                            p_win->sec_sum_window_out_bps += rescale(win_out_bytes * 8, TSS_SYS_CLOCK_FREQ, win_duration);
                            p_win->sec_sum_window_in_bps += rescale(win_in_bytes * 8, TSS_SYS_CLOCK_FREQ, win_duration);
                            p_win->sec_sum_window_drop_bps += rescale(win_drop_bytes * 8, TSS_SYS_CLOCK_FREQ, win_duration);
                            p_win->sec_sum_window_pkts += win_out_bytes / TSS_TS_PACKET_SIZE;
                            p_win->sec_num_windows++;
                            p_win->window_start_emitted_bytes = p_win->mux_output_bytes;
                            p_win->window_start_rx_bytes = p_win->rx_bytes_total;
                            p_win->window_start_accept_bytes = p_win->fifo_accept_bytes;
                            p_win->window_start_drop_bytes = p_win->drop_bytes_total;
                        }
                    }
                }
                prog->pcr_window_start_pcr = pcr;
                prog->pcr_count = 0;
                ctx->tel_trigger_metrology = 1;
            }
        }

        if (pid->type == TSS_PID_PSI) {
            ctx->dbg_cnt_si++;
            ctx->psi_consecutive_count++;
        } else {
            ctx->dbg_cnt_payload++;
            ctx->psi_consecutive_count = 0;
        }

        if (ctx->cfg.write_cb) ctx->cfg.write_cb(ctx->cfg.write_opaque, pkt, TSS_TS_PACKET_SIZE);
        pid->mux_output_bytes += TSS_TS_PACKET_SIZE;
        pid->tb_fullness_bits += (int64_t)TSS_TS_PACKET_BITS;
        pid->tokens_bits -= (int64_t)TSS_TS_PACKET_BITS;
        pid->continuous_fullness_bits = FFMAX(0, pid->continuous_fullness_bits - (int64_t)TSS_TS_PACKET_BITS);
        if (ctx->last_pid == pid) {
            pid->burst_count++;
        } else {
            if (ctx->last_pid) ctx->last_pid->burst_count = 0;
            pid->burst_count = 1;
        }
        ctx->last_pid = pid;
        useful = 1;
    }

post_emission:
    ctx->physical_stc = next_physical_stc;
    tss_refill_tokens_flywheel(ctx);

    ctx->next_slot_stc = ctx->physical_stc + ctx->ticks_per_packet;
    ctx->total_bytes_written += TSS_TS_PACKET_SIZE;
    ctx->packet_count++;

    for (i = 0; i < ctx->nb_all_pids; i++) {
        tss_pid_state_t *p = ctx->all_pids[i];
        if (p && p->au_events && p->vbv_size_bits > 0) {
            if (p->buffer_level_bits > p->vbv_size_bits * 98 / 100) {
                if (!p->is_panic_mode) {
                    tss_log(ctx, TSS_LOG_WARN, "[T-STD] PID 0x%04x VBV PANIC: Model %.1f%% full. Entering Emergency Drain.\n",
                           p->pid, (double)p->buffer_level_bits * 100.0 / p->vbv_size_bits);
                    p->is_panic_mode = 1;
                }
            } else if (p->is_panic_mode && p->buffer_level_bits < p->vbv_size_bits * 90 / 100) {
                tss_log(ctx, TSS_LOG_INFO, "[T-STD] PID 0x%04x VBV RECOVERY: Model %.1f%%. Resuming normal pacing.\n",
                       p->pid, (double)p->buffer_level_bits * 100.0 / p->vbv_size_bits);
                p->is_panic_mode = 0;
            }
        }
    }

    if (ctx->v_stc - ctx->next_pat_ts >= ctx->pat_period) {
        if (ctx->cfg.si_cb) ctx->cfg.si_cb(ctx->cfg.write_opaque, 1, 0, ctx->v_stc);
        ctx->next_pat_ts = ctx->v_stc;
    }
    if (ctx->v_stc - ctx->next_sdt_ts >= ctx->sdt_period) {
        if (ctx->cfg.si_cb) ctx->cfg.si_cb(ctx->cfg.write_opaque, 0, 1, ctx->v_stc);
        ctx->next_sdt_ts = ctx->v_stc;
    }

    for (i = 0; i < ctx->nb_all_pids; i++) {
        tss_pid_state_t *p = ctx->all_pids[i];
        if (p && p->au_events) {
            while (tss_fifo_size(p->au_events) >= sizeof(tss_access_unit_t)) {
                tss_access_unit_t au;
                tss_fifo_peek(p->au_events, (uint8_t*)&au, sizeof(tss_access_unit_t), 0);
                if (ctx->v_stc >= au.dts) {
                    p->buffer_level_bits = (p->buffer_level_bits > au.size_bits) ? p->buffer_level_bits - au.size_bits : 0;
                    tss_fifo_drain(p->au_events, sizeof(tss_access_unit_t));
                } else break;
            }
        }
    }

    tss_account_metrology(ctx);
    return useful;
}

void tsshaper_drive(tsshaper_t *ctx)
{
    int64_t target_stc;
    int steps = 0;
    if (!ctx || ctx->v_stc == TSS_NOPTS_VALUE || ctx->in_drive)
        return;

    ctx->in_drive = 1;

    target_stc = ctx->max_dts_seen - ctx->mux_delay_27m;

    if ((ctx->last_log_stc == TSS_NOPTS_VALUE ||
         ctx->v_stc - ctx->last_log_stc >= 10LL * TSS_SYS_CLOCK_FREQ) && ctx->cfg.debug_level >= 2) {
        tss_log(ctx, TSS_LOG_DEBUG, "[T-STD] Drive: vSTC=%"PRId64"ms, targetSTC=%"PRId64"ms, maxDTS=%"PRId64"ms, delay=%"PRId64"ms\n",
               ctx->v_stc / 27000, target_stc / 27000,
               ctx->max_dts_seen / 27000, ctx->mux_delay_27m * 12 / 10 / 27000);
        ctx->last_log_stc = ctx->v_stc;
    }

    if (target_stc - ctx->v_stc > TSS_SYS_CLOCK_FREQ * 3) {
        tss_log(ctx, TSS_LOG_WARN, "[T-STD] DRIVE FUSE (LAG): Catching up %"PRId64"ms skew (vSTC=%"PRId64", target=%"PRId64", maxDTS=%"PRId64")\n",
               (target_stc - ctx->v_stc) / 27000,
               ctx->v_stc / 27000, target_stc / 27000, ctx->max_dts_seen / 27000);
        ctx->stc_offset += (target_stc - ctx->v_stc);
        ctx->v_stc = target_stc;
        ctx->stc.base = target_stc;
        ctx->stc.rem  = 0;
        ctx->physical_stc = target_stc;
        ctx->last_refill_physical_stc = target_stc;
        ctx->next_slot_stc = target_stc;
        ctx->jump_occurred = 1;

        for (int i = 0; i < ctx->nb_all_pids; i++) {
            if (ctx->all_pids[i]) {
                ctx->all_pids[i]->tokens_bits = 0;
                ctx->all_pids[i]->last_update_ts = ctx->v_stc;
                ctx->all_pids[i]->token_remainder = 0;
            }
        }
    } else if (ctx->v_stc - target_stc > TSS_SYS_CLOCK_FREQ * 3) {
        if (ctx->packet_count % 1000 == 0) {
            tss_log(ctx, TSS_LOG_WARN, "[T-STD] DRIVE SKIP: Output %"PRId64"ms ahead of source. Physical pipe too wide or input stall.\n",
                   ((ctx->v_stc - target_stc) / 27000));
        }
    }

    while (ctx->v_stc < target_stc && steps++ < TSS_MAX_DRIVE_STEPS)
        tss_step_internal(ctx);

    ctx->in_drive = 0;
}

void tsshaper_drain(tsshaper_t *ctx)
{
    int steps = 0, has_data = 1;
    int64_t start_v_stc;

    if (!ctx || ctx->v_stc == TSS_NOPTS_VALUE)
        return;

    ctx->in_drain = 1;
    ctx->drain_start_vstc = ctx->v_stc;
    start_v_stc = ctx->v_stc;
    if (ctx->cfg.debug_level) {
        tss_log(ctx, TSS_LOG_INFO, "[T-STD DRAIN START] vSTC=%"PRId64"ms. Checking remaining data in FIFO...\n", (start_v_stc / 27000));
    }

    while (has_data) {
        if (ctx->v_stc - start_v_stc > 2LL * TSS_SYS_CLOCK_FREQ) {
             tss_log(ctx, TSS_LOG_WARN, "[T-STD DRAIN] Timeout (2.0s). vSTC=%"PRId64"ms. Force truncating.\n", (ctx->v_stc / 27000));
             break;
        }

        has_data = 0;
        for (int i = 0; i < ctx->nb_all_pids; i++) {
            tss_pid_state_t *p = ctx->all_pids[i];
            if ((p->type == TSS_PID_VIDEO || p->type == TSS_PID_AUDIO) && p->ts_fifo && tss_fifo_size(p->ts_fifo) >= TSS_TS_PACKET_SIZE) {
                if (ctx->cfg.debug_level>=2 && steps % 100 == 0 ) {
                    tss_log(ctx, TSS_LOG_INFO, "[T-STD DRAIN] Step %d: PID 0x%x FIFO remaining %d bytes, LastDTS=%" PRId64 "ms\n",
                           steps, p->pid, tss_fifo_size(p->ts_fifo), p->last_dts_raw / 27000);
                }
                has_data = 1;
                break;
            }
        }
        if (!has_data) {
            int64_t final_stc = ctx->max_dts_seen;
            if (ctx->v_stc < final_stc) {
                has_data = 1;
            }
        }

        if (has_data) {
            tss_step_internal(ctx);
            steps++;
        }
    }

    tss_print_summary(ctx, start_v_stc, steps);
}

void tsshaper_reset_timeline(tsshaper_t *ctx) {
    if (!ctx) return;
    ctx->v_stc = TSS_NOPTS_VALUE; ctx->physical_stc = TSS_NOPTS_VALUE; ctx->dts_epoch_invalid = 1;
    for (int i = 0; i < ctx->nb_all_pids; i++) { tss_fifo_reset(ctx->all_pids[i]->ts_fifo); ctx->all_pids[i]->last_dts_raw = TSS_NOPTS_VALUE; }
}

tsshaper_t* tsshaper_create(const tsshaper_config_t* cfg)
{
    tsshaper_t *tstd = calloc(1, sizeof(tsshaper_t));
    if (!tstd) return NULL;

    tstd->cfg = *cfg;
    tstd->first_dts = TSS_NOPTS_VALUE;
    tstd->v_stc = TSS_NOPTS_VALUE;
    tstd->physical_stc = TSS_NOPTS_VALUE;
    tstd->last_refill_physical_stc = TSS_NOPTS_VALUE;
    tstd->last_log_stc = TSS_NOPTS_VALUE;
    tstd->tel_last_1s_stc = TSS_NOPTS_VALUE;
    tstd->tel_sec_start_stc = TSS_NOPTS_VALUE;

    tstd->stc_offset = 0;
    tstd->mux_rate = cfg->mux_rate;

    tstd->stc.base = TSS_NOPTS_VALUE;
    tstd->stc.rem  = 0;
    tstd->stc.den  = cfg->mux_rate;

    tstd->ticks_per_packet = (8LL * TSS_TS_PACKET_SIZE * TSS_SYS_CLOCK_FREQ) / tstd->mux_rate;
    tstd->rem_per_packet   = (8LL * TSS_TS_PACKET_SIZE * TSS_SYS_CLOCK_FREQ) % tstd->mux_rate;

    tstd->mux_delay_27m = (cfg->mux_delay_ms > 0 ? (int64_t)cfg->mux_delay_ms : 700LL) * 27000LL;
    tstd->max_fifo_size = FFMAX((tstd->mux_rate * (cfg->mux_delay_ms > 0 ? cfg->mux_delay_ms : 700) * TSS_JUMP_THRESHOLD_SEC) / 8000, 4 * 1024 * 1024);
    if (tstd->max_fifo_size > 10 * 1024 * 1024) tstd->max_fifo_size = 10 * 1024 * 1024;

    tstd->nb_programs = cfg->nb_streams ? cfg->nb_streams : 1;
    tstd->programs = calloc(tstd->nb_programs, sizeof(tss_program_t *));
    tstd->stream_index_to_pid = calloc(cfg->nb_streams > 0 ? cfg->nb_streams : 1, sizeof(tss_pid_state_t *));
    tstd->all_pids = calloc(8192, sizeof(tss_pid_state_t *));

    return tstd;
}

int tsshaper_add_pid(tsshaper_t* tstd, uint16_t pid_num, tss_pid_type_t type, uint64_t br) {
    if (!tstd || pid_num >= 8192) return -1;
    tss_pid_state_t *pid = calloc(1, sizeof(tss_pid_state_t));
    if (!pid) return -1;

    pid->pid = pid_num;
    pid->type = type;
    pid->allocated_cbr_rate = br;
    pid->tb_size_bits = TSS_TB_SIZE_STANDARD * 8;

    if (type == TSS_PID_VIDEO) {
        int64_t pcr_overhead_bps = (tstd->cfg.pcr_period_ms > 0) ? (64000 / tstd->cfg.pcr_period_ms) : 2133;
        int64_t ts_header_overhead_bps = (br * 4 / 184);
        int64_t pes_overhead_bps = 50 * 152;
        int64_t adaptive_ms;

        pid->refill_rate_base = br + ts_header_overhead_bps + pcr_overhead_bps + pes_overhead_bps;
        pid->refill_rate_base = pid->refill_rate_base * 1050 / 1000;
        pid->base_refill_rate_bps = pid->refill_rate_base;
        pid->nominal_refill_bps = pid->refill_rate_base;
        pid->bitrate_bps = pid->refill_rate_base;

        if (br <= 800000) { adaptive_ms = 10; }
        else if (br >= 1600000) { adaptive_ms = 20; }
        else { adaptive_ms = 10 + (br - 800000) * 10 / 800000; }

        pid->bucket_size_bits = FFMAX(TSS_TS_PACKET_BITS * 8, (int64_t)(br * adaptive_ms / 1000));
        pid->rx_rate_bps = FFMAX(tstd->mux_rate * 120 / 100, 20000000);
        pid->fifo_capacity = tstd->max_fifo_size;
        pid->tb_size_bits = 3 * (int64_t)TSS_TS_PACKET_BITS;
    } else if (type == TSS_PID_AUDIO) {
        pid->base_refill_rate_bps = TSS_RX_RATE_AUDIO;
        pid->bucket_size_bits = 3 * (int64_t)TSS_TS_PACKET_BITS;
        pid->rx_rate_bps = TSS_RX_RATE_AUDIO;
        pid->fifo_capacity = 1024 * 1024;
    } else {
        pid->base_refill_rate_bps = 128000;
        pid->rx_rate_bps = TSS_RX_RATE_SYS;
        pid->bucket_size_bits = TSS_TS_PACKET_BITS * 16;
        pid->fifo_capacity = TSS_PSI_FIFO_SIZE;
    }

    pid->tokens_bits = FFMIN(pid->bucket_size_bits, TSS_TS_PACKET_BITS * 2);
    pid->next_pacing_stc = 0;
    pid->pi_integral = 0;
    pid->pacing_tokens = TSS_PREC_SCALE;

    pid->ts_fifo = tss_fifo_alloc(pid->fifo_capacity);
    pid->vbv_size_bits = pid->fifo_capacity * 8;

    if (type == TSS_PID_VIDEO || type == TSS_PID_AUDIO) {
        pid->au_events = tss_fifo_alloc(8192 * sizeof(tss_access_unit_t));
    }

    pid->last_dts_raw = TSS_NOPTS_VALUE;
    pid->tel_sec_slack_min_27m = 10LL * TSS_SYS_CLOCK_FREQ;
    pid->buffer_level_bits = (pid->bitrate_bps * 100) / 1000;
    pid->continuous_fullness_bits = pid->buffer_level_bits;
    if (type != TSS_PID_AUDIO) {
        pid->buffer_level_bits = FFMAX(TSS_BUFFER_FLOOR_BITS, pid->buffer_level_bits);
    }

    pid->last_update_ts = TSS_NOPTS_VALUE;
    pid->cc = 15;

    tstd->all_pids[tstd->nb_all_pids++] = pid;
    tstd->pid_map[pid_num] = pid;
    return 0;
}

void tsshaper_destroy(tsshaper_t* ctx) {
    if (!ctx) return;
    for (int i = 0; i < ctx->nb_all_pids; i++) {
        tss_pid_state_t *p = ctx->all_pids[i];
        if (p) { tss_fifo_free(p->ts_fifo); tss_fifo_free(p->au_events); free(p); }
    }
    free(ctx->all_pids);
    free(ctx->programs);
    free(ctx->stream_index_to_pid);
    free(ctx);
}
