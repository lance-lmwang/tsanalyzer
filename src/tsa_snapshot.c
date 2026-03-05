#include "tsa_internal.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

static void tsa_calc_physical_bitrate(tsa_handle_t* h, uint64_t n, uint64_t stc, uint64_t dt) {
    uint64_t dp = h->live->total_ts_packets - h->prev_snap_base->total_ts_packets;
    if (dp > 0) {
        uint64_t instant_br = (uint64_t)(((unsigned __int128)dp * 1504 * 1000000000ULL) / dt);
        if (h->live->physical_bitrate_bps == 0)
            h->live->physical_bitrate_bps = instant_br;
        else
            h->live->physical_bitrate_bps = (uint64_t)(0.3 * instant_br + 0.7 * h->live->physical_bitrate_bps);
        if (h->live->physical_bitrate_bps > 10000000000ULL) h->live->physical_bitrate_bps = 10000000000ULL;
    }
    h->live->mdi_mlr_pkts_s = (double)((h->live->cc_loss_count - h->prev_snap_base->cc_loss_count) * 1000000000ULL) / dt;
    h->live->mdi_df_ms = (double)h->live->pcr_jitter_max_ns / 1000000.0;
    h->live->stream_utc_ms = stc / 1000000ULL;
}

static void tsa_eval_tr101290_alarms(tsa_handle_t* h, uint64_t n, uint64_t stc) {
    if (!h->stc_locked) {
        h->live->alarm_sync_loss = !h->signal_lock;
        h->live->alarm_cc_error = (h->live->cc_error.count > h->prev_snap_base->cc_error.count);
        h->live->alarm_pcr_accuracy_error = (h->live->pcr_accuracy_error.count > h->prev_snap_base->pcr_accuracy_error.count);
        h->live->alarm_crc_error = (h->live->crc_error.count > h->prev_snap_base->crc_error.count);
        return;
    }

    uint64_t pat_vstc_dt = stc - h->live->pid_last_seen_vstc[0];
    if (h->pid_seen[0] && pat_vstc_dt > 500000000ULL) {
        h->live->pat_error.count++;
        h->live->pat_error.last_timestamp_ns = n;
        h->live->alarm_pat_error = true;
        h->pid_status[0] = TSA_STATUS_INVALID;
        tsa_push_event(h, TSA_EVENT_PAT_TIMEOUT, 0, 0);
    } else {
        h->live->alarm_pat_error = false;
    }

    bool pmt_missing = false;
    for (int i = 0; i < MAX_PROGRAMS; i++) {
        uint16_t ppid = h->programs[i].pmt_pid;
        if (ppid > 0 && ppid < TS_PID_MAX) {
            uint64_t pmt_vstc_dt = stc - h->live->pid_last_seen_vstc[ppid];
            if (pmt_vstc_dt > 500000000ULL) {
                pmt_missing = true;
                h->pid_status[ppid] = TSA_STATUS_INVALID;
                tsa_push_event(h, TSA_EVENT_PMT_TIMEOUT, ppid, 0);
            }
        }
    }
    h->live->alarm_pmt_error = pmt_missing;
    if (pmt_missing) {
        h->live->pmt_error.count++;
        h->live->pmt_error.last_timestamp_ns = n;
    }

    uint64_t sdt_vstc_dt = stc - h->live->pid_last_seen_vstc[0x11];
    if (h->pid_seen[0x11] && sdt_vstc_dt > 2000000000ULL) {
        h->live->sdt_error.count++;
        h->live->sdt_error.last_timestamp_ns = n;
        h->live->alarm_sdt_error = true;
        h->pid_status[0x11] = TSA_STATUS_DEGRADED;
    } else {
        h->live->alarm_sdt_error = false;
    }

    bool pcr_repetition_error = false;
    for (int i = 0; i < MAX_PROGRAMS; i++) {
        uint16_t pcr_pid = h->programs[i].pcr_pid;
        if (pcr_pid > 0 && pcr_pid < TS_PID_MAX) {
            if (h->clock_inspectors[pcr_pid].initialized) {
                int64_t elapsed_ns = (int64_t)(n - h->clock_inspectors[pcr_pid].last_pcr_local_ns);
                if (elapsed_ns > 40000000LL) {
                    pcr_repetition_error = true;
                    h->live->pcr_repetition_error.count++;
                    h->live->pcr_repetition_error.last_timestamp_ns = n;
                    tsa_push_event(h, TSA_EVENT_PCR_REPETITION, pcr_pid, (uint64_t)(elapsed_ns / 1000000));
                }
            }
        }
    }
    h->live->alarm_pcr_repetition_error = pcr_repetition_error;

    h->live->alarm_sync_loss = !h->signal_lock;
    h->live->alarm_cc_error = (h->live->cc_error.count > h->prev_snap_base->cc_error.count);
    h->live->alarm_pcr_accuracy_error = (h->live->pcr_accuracy_error.count > h->prev_snap_base->pcr_accuracy_error.count);
    h->live->alarm_crc_error = (h->live->crc_error.count > h->prev_snap_base->crc_error.count);
}

static void tsa_eval_pcr_drift(tsa_handle_t* h, double* out_sl, double* out_l_sl, double* out_rn, double* out_re, double* out_cn, double* out_ce) {
    int64_t pa = 0;
    double sl = 1.0, ic_d = 0.0;
    const double Q64 = 18446744073709551616.0;
    
    if (ts_pcr_window_regress(&h->pcr_window, &sl, &ic_d, &pa) == 0) {
        double instant_drift = (sl - 1.0) * 1000000.0;
        h->live->pcr_drift_ppm = (h->live->pcr_drift_ppm * 0.99) + (instant_drift * 0.01);
        if (h->op_mode == TSA_MODE_REPLAY) {
            h->live->pcr_drift_ppm = 0.0;
            h->stc_wall_drift_ppm = 0.0;
        } else {
            h->stc_wall_drift_ppm = h->live->pcr_drift_ppm;
        }
        h->live->pcr_accuracy_ns = (double)pa;
        h->stc_slope_q64 = (int128_t)(sl * Q64);
        h->stc_intercept_q64 = (int128_t)(ic_d * Q64);
    } else {
        h->live->pcr_drift_ppm = 0.0;
        h->stc_wall_drift_ppm = 0.0;
    }
    
    double l_sl = sl, l_ic = 0.0;
    int64_t l_pa = 0;
    if (ts_pcr_window_regress(&h->pcr_long_window, &l_sl, &l_ic, &l_pa) == 0) {
        h->long_term_drift_ppm = (l_sl - 1.0) * 1000000.0;
        if (h->op_mode == TSA_MODE_REPLAY) h->long_term_drift_ppm = 0.0;
    } else {
        h->long_term_drift_ppm = 0.0;
    }

    double rn = 999.0, re = 999.0;
    if (h->live->pcr_bitrate_bps > h->live->physical_bitrate_bps && h->live->physical_bitrate_bps > 0) {
        uint32_t la = h->srt_live.effective_rcv_latency_ms ? h->srt_live.effective_rcv_latency_ms : 50,
                 ji = (uint32_t)(h->live->pcr_jitter_max_ns / 1000000ULL);
        if (la > ji)
            rn = (double)((uint64_t)(la - ji) * h->live->pcr_bitrate_bps / 1000) /
                 (double)(h->live->pcr_bitrate_bps - h->live->physical_bitrate_bps);
        else
            rn = 0.0;
    }
    double dr = fabs(l_sl - 1.0);
    if (dr > 0.000001) re = (100.0 - h->live->pcr_accuracy_ns / 1000000.0) / dr / 1000.0;

    double cn = 0, ce = 0;
    if (h->live->mdi_mlr_pkts_s > 0) cn += 0.8;
    if (rn < 5.0) cn += (5.0 - rn) / 5.0;
    if (h->live->pcr_jitter_max_ns > 10000000ULL)
        ce += 0.8;
    else if (h->live->pcr_jitter_max_ns > 500000ULL)
        ce += 0.5;
    if (h->live->pcr_accuracy_error.count > h->prev_snap_base->pcr_accuracy_error.count) ce += 0.4;

    *out_sl = sl; *out_l_sl = l_sl; *out_rn = rn; *out_re = re; *out_cn = cn; *out_ce = ce;
}

static uint32_t tsa_update_pid_metrics(tsa_handle_t* h, tsa_snapshot_full_t* sn, uint64_t dt) {
    h->live->video_fps = 0;
    h->live->gop_ms = 0;

    uint32_t ptc = h->pid_tracker_count;
    if (ptc > MAX_ACTIVE_PIDS) ptc = MAX_ACTIVE_PIDS;

    uint32_t ai = 0;
    for (uint32_t i = 0; i < ptc; i++) {
        uint16_t p = h->pid_active_list[i];
        if (p >= TS_PID_MAX) continue;

        uint64_t p_pd = h->live->pid_packet_count[p] - h->prev_snap_base->pid_packet_count[p];
        if (p_pd > 0 && dt >= 1000000ULL) {
            uint64_t cb = (p_pd * 1504 * 1000000000ULL) / dt;
            if (cb > 10000000000ULL) cb = 10000000000ULL;
            if (h->live->pid_bitrate_bps[p] == 0)
                h->live->pid_bitrate_bps[p] = cb;
            else
                h->live->pid_bitrate_bps[p] =
                    (cb * (uint64_t)h->pcr_ema_alpha_q32 +
                     h->live->pid_bitrate_bps[p] * ((1ULL << 32) - (uint64_t)h->pcr_ema_alpha_q32)) >>
                    32;
        }

        const char* p_st = tsa_get_pid_type_name(h, p);
        if (strcmp(p_st, "H.264") == 0 || strcmp(p_st, "HEVC") == 0 || strcmp(p_st, "MPEG2-V") == 0) {
            if (h->pid_gop_ms[p] > 0 && h->pid_last_gop_n[p] > 0) {
                h->live->video_fps = (float)h->pid_last_gop_n[p] * 1000.0f / h->pid_gop_ms[p];
                h->live->gop_ms = h->pid_gop_ms[p];
            }
        }

        sn->pids[ai].pid = p;
        strncpy(sn->pids[ai].type_str, p_st, 15);
        sn->pids[ai].bitrate_q16_16 = (int64_t)h->live->pid_bitrate_bps[p] << 16;
        sn->pids[ai].status = (uint8_t)h->pid_status[p];
        sn->pids[ai].width = h->pid_width[p];
        sn->pids[ai].height = h->pid_height[p];
        sn->pids[ai].profile = h->pid_profile[p];
        sn->pids[ai].audio_sample_rate = h->pid_audio_sample_rate[p];
        sn->pids[ai].audio_channels = h->pid_audio_channels[p];
        sn->pids[ai].gop_n = h->pid_last_gop_n[p];
        sn->pids[ai].gop_min = h->pid_gop_min[p];
        sn->pids[ai].gop_max = h->pid_gop_max[p];
        sn->pids[ai].gop_ms = h->pid_gop_ms[p];
        sn->pids[ai].i_frames = h->pid_i_frames[p];
        sn->pids[ai].p_frames = h->pid_p_frames[p];
        sn->pids[ai].b_frames = h->pid_b_frames[p];
        sn->pids[ai].has_cea708 = h->pid_has_cea708[p];
        sn->pids[ai].has_scte35 = h->pid_is_scte35[p];
        sn->pids[ai].eb_fill_pct = (float)((double)(h->pid_eb_fill_q64[p] >> 64) * 100.0 / 1200000.0);
        ai++;
    }
    return ai;
}

void tsa_commit_snapshot(tsa_handle_t* h, uint64_t n) {
    if (!h) return;
    if (n == 0) n = h->last_snap_ns;
    if (!h->stc_locked) h->stc_ns = n;

    uint8_t a = atomic_load_explicit(&h->double_buffer.active_idx, memory_order_acquire);
    uint8_t inactive_idx = a ^ 1;
    tsa_snapshot_full_t* sn = h->double_buffer.buffers[inactive_idx];

    for (uint32_t i = 0; i < h->pid_tracker_count; i++) {
        tsa_tstd_drain(h, h->pid_active_list[i]);
    }

    uint64_t stc = h->stc_ns, dt = stc - h->last_snap_ns;
    if (dt == 0 || dt > 100000000000ULL) {
        dt = 100000000ULL;
        for (int i = 0; i < TS_PID_MAX; i++) {
            h->live->pid_last_seen_vstc[i] = stc;
            h->live->pid_last_seen_ns[i] = n;
        }
    }
    if (dt < 1000000ULL) dt = 1000000ULL;

    tsa_calc_physical_bitrate(h, n, stc, dt);
    tsa_eval_tr101290_alarms(h, n, stc);

    double sl = 1.0, l_sl = 1.0, rn = 999.0, re = 999.0, cn = 0.0, ce = 0.0;
    tsa_eval_pcr_drift(h, &sl, &l_sl, &rn, &re, &cn, &ce);

    uint32_t ai = tsa_update_pid_metrics(h, sn, dt);

    float current_health = 100.0f;
    if (!h->signal_lock) {
        current_health = 0.0f;
    } else {
        if (rn < 5.0f) current_health -= (5.0f - rn) * 4.0f;
        if (re < 30.0f) current_health -= (30.0f - re) * 0.5f;
        if (h->live->cc_error.count > h->prev_snap_base->cc_error.count) current_health -= 25.0f;
    }
    if (current_health < 0) current_health = 0;
    if (current_health < h->last_health_score || h->last_health_score < 0.1)
        h->last_health_score = current_health;
    else
        h->last_health_score = h->last_health_score * 0.8f + current_health * 0.2f;

    memset(&sn->predictive, 0, sizeof(sn->predictive));
    sn->predictive.master_health = h->last_health_score;
    sn->predictive.rst_network_s = rn;
    sn->predictive.rst_encoder_s = re;
    sn->predictive.stc_wall_drift_ppm = h->stc_wall_drift_ppm;
    sn->predictive.long_term_drift_ppm = h->long_term_drift_ppm;
    sn->predictive.stc_drift_slope = sl;
    sn->predictive.pcr_jitter_ns = h->live->pcr_jitter_max_ns;
    sn->predictive.stc_locked_bool = h->stc_locked ? 1 : 0;
    
    sn->predictive.fault_domain = (cn > 0.6 && ce < 0.2)   ? 1
                                  : (ce > 0.6 && cn < 0.2) ? 2
                                  : (cn > 0.4 && ce > 0.4) ? 3
                                                           : 0;

    sn->summary.master_health = h->last_health_score;
    sn->summary.total_packets = h->live->total_ts_packets;
    sn->summary.signal_lock = h->signal_lock;
    sn->summary.physical_bitrate_bps = h->live->physical_bitrate_bps;

    strncpy(sn->network_name, h->network_name, 255);
    strncpy(sn->service_name, h->service_name, 255);
    strncpy(sn->provider_name, h->provider_name, 255);
    sn->active_pid_count = ai;

    sn->stats = *h->live;
    atomic_store_explicit(&h->double_buffer.active_idx, inactive_idx, memory_order_release);
    *h->prev_snap_base = *h->live;
    h->last_snap_ns = stc;
    h->live->pcr_jitter_max_ns = 0;
}

int tsa_take_snapshot_full(tsa_handle_t* h, tsa_snapshot_full_t* s) {
    if (!h || !s) return -1;
    uint8_t a = atomic_load_explicit(&h->double_buffer.active_idx, memory_order_acquire);
    *s = *h->double_buffer.buffers[a];
    return 0;
}

int tsa_take_snapshot_lite(tsa_handle_t* h, tsa_snapshot_lite_t* s) {
    if (!h || !s) return -1;
    s->total_packets = h->live->total_ts_packets;
    s->physical_bitrate_bps = h->live->physical_bitrate_bps;
    s->active_pid_count = h->pid_tracker_count;
    s->signal_lock = h->signal_lock;
    uint8_t a = atomic_load_explicit(&h->double_buffer.active_idx, memory_order_acquire);
    s->master_health = h->double_buffer.buffers[a]->summary.master_health;
    return 0;
}
