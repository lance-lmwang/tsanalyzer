#include "tss_internal.h"
#include <stdlib.h>
#include <string.h>

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

tsshaper_t* tsshaper_create(const tsshaper_config_t* cfg) {
    tsshaper_t *ctx = calloc(1, sizeof(tsshaper_t));
    if (!ctx) return NULL;
    ctx->cfg = *cfg;
    ctx->v_stc = TSS_NOPTS_VALUE;
    ctx->physical_stc = TSS_NOPTS_VALUE;
    ctx->last_refill_physical_stc = TSS_NOPTS_VALUE;
    ctx->tel_last_1s_stc = TSS_NOPTS_VALUE;
    ctx->stc.base = TSS_NOPTS_VALUE;
    ctx->stc.den = cfg->mux_rate;
    ctx->ticks_per_packet = (8LL * TSS_TS_PACKET_SIZE * TSS_SYS_CLOCK_FREQ) / cfg->mux_rate;
    ctx->rem_per_packet   = (8LL * TSS_TS_PACKET_SIZE * TSS_SYS_CLOCK_FREQ) % cfg->mux_rate;
    ctx->next_pat_ts = -(int64_t)(100 * 27000);
    ctx->next_sdt_ts = -(int64_t)(500 * 27000);
    return ctx;
}

void tsshaper_destroy(tsshaper_t* ctx) {
    if (!ctx) return;
    for (int i = 0; i < ctx->nb_all_pids; i++) {
        tss_pid_state_t *p = ctx->all_pids[i];
        if (p) { tss_fifo_free(p->ts_fifo); tss_fifo_free(p->au_events); free(p); }
    }
    free(ctx->all_pids);
    free(ctx);
}

int tsshaper_add_pid(tsshaper_t* ctx, uint16_t pid, tss_pid_type_t type, uint64_t br) {
    if (!ctx || pid >= 8192) return -1;
    tss_pid_state_t *p = calloc(1, sizeof(tss_pid_state_t));
    if (!p) return -1;
    p->pid = pid; p->type = type; p->allocated_cbr_rate = br;
    p->tb_size_bits = TSS_TB_SIZE_STANDARD * 8;
    p->last_dts_raw = TSS_NOPTS_VALUE; p->last_update_ts = TSS_NOPTS_VALUE;
    p->cc = 15;
    if (type == TSS_PID_VIDEO) {
        p->bitrate_bps = br;
        p->rx_rate_bps = (ctx->cfg.mux_rate * 120 / 100 > 20000000) ? ctx->cfg.mux_rate * 120 / 100 : 20000000;
        p->fifo_capacity = 4 * 1024 * 1024;
        p->tb_size_bits = 3 * (int64_t)TSS_TS_PACKET_BITS;
        p->base_refill_rate_bps = br * 105 / 100;
        p->bucket_size_bits = (br * 200 / 1000);
        p->au_events = tss_fifo_alloc(8192 * sizeof(tss_access_unit_t));
    } else if (type == TSS_PID_AUDIO) {
        p->bitrate_bps = br; p->base_refill_rate_bps = br * 110 / 100;
        p->rx_rate_bps = TSS_RX_RATE_AUDIO; p->fifo_capacity = 1024 * 1024;
        p->bucket_size_bits = 3 * (int64_t)TSS_TS_PACKET_BITS;
        p->au_events = tss_fifo_alloc(8192 * sizeof(tss_access_unit_t));
    } else {
        p->base_refill_rate_bps = 128000; p->rx_rate_bps = TSS_RX_RATE_SYS;
        p->fifo_capacity = TSS_PSI_FIFO_SIZE; p->bucket_size_bits = TSS_TS_PACKET_BITS * 16;
    }
    p->ts_fifo = tss_fifo_alloc(p->fifo_capacity);
    p->tokens_bits = TSS_TS_PACKET_BITS * 2; p->pacing_tokens = TSS_PREC_SCALE;
    ctx->all_pids = realloc(ctx->all_pids, (ctx->nb_all_pids + 1) * sizeof(tss_pid_state_t *));
    ctx->all_pids[ctx->nb_all_pids++] = p;
    ctx->pid_map[pid] = p;
    return 0;
}

void tsshaper_enqueue_dts(tsshaper_t* ctx, uint16_t pid_num, int64_t dts_27m, bool is_key) {
    tss_pid_state_t *pid = ctx->pid_map[pid_num];
    if (!ctx || !pid || dts_27m == TSS_NOPTS_VALUE) return;
    if (ctx->v_stc == TSS_NOPTS_VALUE || (ctx->dts_epoch_invalid && is_key)) {
        int64_t mux_delay_27m = (int64_t)ctx->cfg.mux_delay_ms * 27000;
        ctx->stc_offset = (mux_delay_27m * 9 / 10) - (100LL * 27000);
        ctx->stc.base = ctx->stc_offset;
        ctx->stc.rem  = 0;
        ctx->v_stc = ctx->stc.base;
        ctx->physical_stc = ctx->v_stc;
        ctx->next_slot_stc = ctx->v_stc;
        ctx->dts_offset = dts_27m - (mux_delay_27m * 9 / 10);
        ctx->max_dts_seen = mux_delay_27m * 9 / 10; ctx->dts_epoch_invalid = false;
        ctx->last_refill_physical_stc = ctx->v_stc;
        for (int i = 0; i < ctx->nb_all_pids; i++) {
            ctx->all_pids[i]->tokens_bits = (ctx->all_pids[i]->bitrate_bps * 200) / 1000;
        }
    }
    int64_t rel_dts = dts_27m - ctx->dts_offset;
    if (pid->last_dts_raw == TSS_NOPTS_VALUE) pid->next_arrival_ts = rel_dts;
    if (pid->au_events) {
        tss_access_unit_t au; au.dts = rel_dts; au.is_key = is_key; au.size_bits = 0;
        tss_fifo_write(pid->au_events, (uint8_t*)&au, sizeof(au));
    }
    pid->last_dts_raw = dts_27m;
    if (rel_dts > ctx->max_dts_seen) ctx->max_dts_seen = rel_dts;
}

void tsshaper_enqueue_ts(tsshaper_t* ctx, uint16_t pid_num, const uint8_t* pkt) {
    tss_pid_state_t *pid = ctx->pid_map[pid_num];
    if (!pid) return;
    tss_fifo_write(pid->ts_fifo, pkt, TSS_TS_PACKET_SIZE);
    pid->fifo_accept_bytes += TSS_TS_PACKET_SIZE;
    pid->continuous_fullness_bits += (int64_t)TSS_TS_PACKET_BITS;
}

static int tss_step_internal(tsshaper_t *ctx) {
    uint8_t pkt[TSS_TS_PACKET_SIZE];
    tss_pid_state_t *pid = NULL;
    int64_t next_v_base, next_v_rem, next_physical_stc;
    int64_t last_physical_for_leak = ctx->physical_stc;

    /* 1:1 Precision Fractional STC Advance */
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

    if (next_physical_stc < ctx->next_slot_stc) return 0;

    ctx->stc.base = next_v_base;
    ctx->stc.rem  = next_v_rem;
    ctx->v_stc    = ctx->stc.base;

    /* 3. TB Leakage */
    if (last_physical_for_leak != TSS_NOPTS_VALUE) {
        int64_t delta = next_physical_stc - last_physical_for_leak;
        for (int i = 0; i < ctx->nb_all_pids; i++) {
            tss_pid_state_t *p = ctx->all_pids[i];
            int64_t work_bits = (delta * p->rx_rate_bps) + p->tb_leak_remainder;
            p->tb_leak_remainder = work_bits % TSS_SYS_CLOCK_FREQ;
            p->tb_fullness_bits = (p->tb_fullness_bits > (work_bits / TSS_SYS_CLOCK_FREQ)) ?
                                   p->tb_fullness_bits - (work_bits / TSS_SYS_CLOCK_FREQ) : 0;
        }
    }
    pid = tss_pick_es_pid(ctx);
    if (pid) {
        if (tss_fifo_read(pid->ts_fifo, pkt, TSS_TS_PACKET_SIZE) == 0) {
            if (ctx->cfg.write_cb) ctx->cfg.write_cb(ctx->cfg.write_opaque, pkt, TSS_TS_PACKET_SIZE);
            pid->mux_output_bytes += TSS_TS_PACKET_SIZE;
            pid->tb_fullness_bits += (int64_t)TSS_TS_PACKET_BITS;
            pid->tokens_bits -= (int64_t)TSS_TS_PACKET_BITS;
            pid->continuous_fullness_bits = (pid->continuous_fullness_bits > TSS_TS_PACKET_BITS) ?
                                             pid->continuous_fullness_bits - TSS_TS_PACKET_BITS : 0;
            pid->au_bits_acc += TSS_TS_PACKET_BITS;
            if (pid->au_events && tss_fifo_size(pid->au_events) >= sizeof(tss_access_unit_t)) {
                tss_access_unit_t head_au;
                if (tss_fifo_peek(pid->au_events, (uint8_t*)&head_au, sizeof(tss_access_unit_t), 0) == 0) {
                    if (ctx->v_stc > (head_au.dts + 27000 * 5)) {
                         tss_fifo_drain(pid->au_events, sizeof(tss_access_unit_t)); pid->au_bits_acc = 0;
                    }
                }
            }
            ctx->last_pid = pid;
            if (pid->type == TSS_PID_VIDEO || pid->type == TSS_PID_AUDIO) ctx->dbg_cnt_payload++; else ctx->dbg_cnt_si++;
        }
    } else {
        memset(pkt, 0xff, TSS_TS_PACKET_SIZE); pkt[0] = 0x47; pkt[1] = 0x1f; pkt[2] = 0xff; pkt[3] = 0x10;
        if (ctx->cfg.write_cb) ctx->cfg.write_cb(ctx->cfg.write_opaque, pkt, TSS_TS_PACKET_SIZE);
        tss_account_null_packet(ctx);
    }
    ctx->physical_stc = next_physical_stc; tss_refill_tokens_flywheel(ctx);
    ctx->next_slot_stc = ctx->physical_stc + ctx->ticks_per_packet;
    ctx->total_bytes_written += TSS_TS_PACKET_SIZE; ctx->packet_count++;
    tss_account_metrology(ctx);
    if (ctx->v_stc - ctx->next_pat_ts >= (int64_t)(100 * 27000)) {
        if (ctx->cfg.si_cb) ctx->cfg.si_cb(ctx->cfg.write_opaque, 1, 0, ctx->v_stc); ctx->next_pat_ts = ctx->v_stc;
    }
    if (ctx->v_stc - ctx->next_sdt_ts >= (int64_t)(500 * 27000)) {
        if (ctx->cfg.si_cb) ctx->cfg.si_cb(ctx->cfg.write_opaque, 0, 1, ctx->v_stc); ctx->next_sdt_ts = ctx->v_stc;
    }
    return 1;
}

void tsshaper_drive(tsshaper_t *ctx) {
    if (!ctx || ctx->v_stc == TSS_NOPTS_VALUE || ctx->in_drive) return;
    ctx->in_drive = true;
    int64_t target_stc = ctx->max_dts_seen - (int64_t)ctx->cfg.mux_delay_ms * 27000;
    if (target_stc - ctx->v_stc > TSS_SYS_CLOCK_FREQ * 3) {
        ctx->v_stc = target_stc; ctx->stc.base = target_stc; ctx->physical_stc = target_stc; ctx->next_slot_stc = target_stc;
    }
    int steps = 0;
    while (ctx->v_stc < target_stc && steps++ < TSS_MAX_DRIVE_STEPS) tss_step_internal(ctx);
    ctx->in_drive = false;
}

void tsshaper_drain(tsshaper_t *ctx) {
    if (!ctx || ctx->v_stc == TSS_NOPTS_VALUE) return;
    ctx->in_drain = true;
    int64_t start_v_stc = ctx->v_stc;
    while (1) {
        if (ctx->v_stc - start_v_stc > 2LL * TSS_SYS_CLOCK_FREQ) break;
        bool has_data = false;
        for (int i = 0; i < ctx->nb_all_pids; i++) {
            if (tss_fifo_size(ctx->all_pids[i]->ts_fifo) >= TSS_TS_PACKET_SIZE) { has_data = true; break; }
        }
        if (!has_data) break;
        tss_step_internal(ctx);
    }
}

void tsshaper_reset_timeline(tsshaper_t *ctx) {
    if (!ctx) return;
    ctx->v_stc = TSS_NOPTS_VALUE; ctx->physical_stc = TSS_NOPTS_VALUE; ctx->dts_epoch_invalid = true;
    for (int i = 0; i < ctx->nb_all_pids; i++) { tss_fifo_reset(ctx->all_pids[i]->ts_fifo); ctx->all_pids[i]->last_dts_raw = TSS_NOPTS_VALUE; }
}
