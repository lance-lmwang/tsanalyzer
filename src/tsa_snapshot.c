#include <math.h>
#include <stdio.h>
#include <string.h>

#include "tsa_internal.h"

static void tsa_calc_stream_bitrate(tsa_handle_t* h, uint64_t wall_dt) {
    /* L2: Ingress Physical Bitrate
     * Use wall-clock time delta for actual throughput measurement. */
    if (wall_dt < 10000000ULL) return; // Need at least 10ms

    uint64_t dp = h->live->total_ts_packets - h->prev_snap_base->total_ts_packets;
    if (dp > 0) {
        uint64_t instant_br = (uint64_t)(((unsigned __int128)dp * TS_PACKET_BITS * NS_PER_SEC) / wall_dt);

        if (h->live->physical_bitrate_bps == 0) {
            if (h->live->total_ts_packets > 1000) h->live->physical_bitrate_bps = instant_br;
        } else {
            /* Smooth ingress bitrate */
            h->live->physical_bitrate_bps = (uint64_t)(0.2 * instant_br + 0.8 * h->live->physical_bitrate_bps);
        }

        if (h->live->physical_bitrate_bps > TS_MAX_BITRATE_BPS) h->live->physical_bitrate_bps = TS_MAX_BITRATE_BPS;
    }

    /* MDI and other clock-relative stats still use STC if possible, but fallback to wall_dt */
    uint64_t stc_dt = h->stc_ns - h->last_snap_ns;
    if (stc_dt < 1000000ULL || stc_dt > 10000000000ULL) stc_dt = wall_dt;

    h->live->mdi_mlr_pkts_s =
        (double)((h->live->cc_loss_count - h->prev_snap_base->cc_loss_count) * NS_PER_SEC) / stc_dt;
    h->live->mdi_df_ms = (double)h->live->pcr_jitter_max_ns / 1000000.0;
    h->live->stream_utc_ms = h->stc_ns / 1000000ULL;
}

static void tsa_eval_tr101290_alarms(tsa_handle_t* h, uint64_t n, uint64_t stc) {
    h->live->alarm_sync_loss = !h->signal_lock;
    h->live->alarm_cc_error = (h->live->cc_error.count > h->prev_snap_base->cc_error.count);

    /* Alarm Suppression: Don't trigger timeouts if the clock isn't moving or during warm-up */
    if (!h->stc_locked || h->live->total_ts_packets < 1000) return;

    uint64_t pat_vstc_dt = (h->live->pid_last_seen_vstc[0] > 0) ? (stc - h->live->pid_last_seen_vstc[0]) : 0;
    if (h->pid_seen[0] && pat_vstc_dt > TSA_TR101290_PAT_TIMEOUT_NS) {
        if (h->live->pat_error.count == 0) h->live->pat_error.first_timestamp_ns = n;
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
        if (ppid > 0 && ppid < TS_PID_MAX && h->pid_seen[ppid]) {
            uint64_t pmt_vstc_dt =
                (h->live->pid_last_seen_vstc[ppid] > 0) ? (stc - h->live->pid_last_seen_vstc[ppid]) : 0;
            if (pmt_vstc_dt > TSA_TR101290_PAT_TIMEOUT_NS) {
                pmt_missing = true;
                h->pid_status[ppid] = TSA_STATUS_INVALID;
                tsa_push_event(h, TSA_EVENT_PMT_TIMEOUT, ppid, 0);
            }
        }
    }
    h->live->alarm_pmt_error = pmt_missing;
    if (pmt_missing) {
        if (h->live->pmt_error.count == 0) h->live->pmt_error.first_timestamp_ns = n;
        h->live->pmt_error.count++;
        h->live->pmt_error.last_timestamp_ns = n;
    }

    // SDT & NIT Active Watchdogs (P3.1, P3.2)
    if (h->seen_pat) {  // Only check if stream seems active
        if (h->last_sdt_ns > 0 && (stc - h->last_sdt_ns) > TSA_TR101290_SDT_TIMEOUT_NS) {
            h->live->sdt_timeout.count++;
            tsa_push_event(h, TSA_EVENT_SDT_TIMEOUT, 0x11, 0);
        }
        if (h->last_nit_ns > 0 && (stc - h->last_nit_ns) > TSA_TR101290_NIT_TIMEOUT_NS) {
            h->live->nit_timeout.count++;
            tsa_push_event(h, TSA_EVENT_NIT_TIMEOUT, 0x10, 0);
        }
    }

    // P3.x Referenced PID Check
    for (int i = 0; i < TS_PID_MAX; i++) {
        if (h->live->pid_is_referenced[i]) {
            uint64_t last_seen_vstc = h->live->pid_last_seen_vstc[i];
            if (last_seen_vstc > 0 && (stc - last_seen_vstc) > TSA_TR101290_PID_TIMEOUT_NS) {  // 5s for PID error
                if (h->live->pid_error.count == 0) h->live->pid_error.first_timestamp_ns = n;
                h->live->pid_error.count++;
                h->live->pid_error.last_timestamp_ns = n;
                tsa_push_event(h, TSA_EVENT_PID_ERROR, i, 0);
            }
        }
    }

    h->live->alarm_pcr_repetition_error =
        (h->live->pcr_repetition_error.count > h->prev_snap_base->pcr_repetition_error.count);
    h->live->alarm_pcr_accuracy_error =
        (h->live->pcr_accuracy_error.count > h->prev_snap_base->pcr_accuracy_error.count);
    h->live->alarm_crc_error = (h->live->crc_error.count > h->prev_snap_base->crc_error.count);
}

void tsa_commit_snapshot(tsa_handle_t* h, uint64_t n) {
    if (!h) return;
    if (n == 0) n = (uint64_t)ts_now_ns128();

    uint64_t stc = h->stc_ns;
    uint64_t dt = stc - h->last_snap_ns;

    /* Safety Reset for Looping/Jumps (100s threshold)
     * Only trigger if STC is already locked to avoid false resets during initialization. */
    if (h->stc_locked && (dt == 0 || dt > (NS_PER_SEC * 100ULL))) {
        dt = 100000000ULL;
        for (int i = 0; i < TS_PID_MAX; i++) {
            h->live->pid_last_seen_vstc[i] = stc;
            h->live->pid_last_seen_ns[i] = n;
        }
    }
    if (dt < 1000000ULL) dt = 1000000ULL;

    uint8_t a = atomic_load_explicit(&h->double_buffer.active_idx, memory_order_acquire);
    uint8_t inactive_idx = a ^ 1;
    tsa_snapshot_full_t* sn = h->double_buffer.buffers[inactive_idx];

    for (uint32_t i = 0; i < h->pid_tracker_count; i++) {
        tsa_tstd_drain(h, h->pid_active_list[i]);
    }

    /* For bitrates and FPS, wall-clock delta is much more stable than STC (which can jump). */
    uint64_t wall_dt = (n > h->last_snap_wall_ns) ? (n - h->last_snap_wall_ns) : 0;
    if (wall_dt < 1000000ULL) wall_dt = 100000000ULL; // Default to 100ms if first snap or jitter

    /* Bitrate is based on wall-clock delta (L2 Tier) */
    tsa_calc_stream_bitrate(h, wall_dt);
    tsa_eval_tr101290_alarms(h, n, stc);

    // --- CAUSAL ANALYSIS ---
    double jitter_ms = (double)h->live->pcr_jitter_max_ns / 1000000.0;
    double drift_abs = fabs(h->stc_wall_drift_ppm);
    float rn = 10.0f - (float)(jitter_ms / 5.0f);
    if (rn < 0) rn = 0;
    float re = 100.0f - (float)(drift_abs * 2.0f);
    if (re < 0) re = 0;
    double cn = jitter_ms / 50.0;
    if (cn > 1.0) cn = 1.0;
    double ce = drift_abs / 100.0;
    if (ce > 1.0) ce = 1.0;

    float current_health = 100.0f;
    if (!h->signal_lock) {
        /* Grace period: Only drop health to 0 after receiving some packets or waiting longer */
        if (h->live->total_ts_packets > 100) current_health = 0.0f;
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
    sn->predictive.stc_drift_slope = FROM_Q64_64(h->stc_slope_q64);
    sn->predictive.pcr_jitter_ns = h->live->pcr_jitter_max_ns;
    sn->predictive.stc_locked_bool = h->stc_locked ? 1 : 0;
    sn->predictive.fault_domain = (cn > 0.6 && ce < 0.2)   ? 1
                                  : (ce > 0.6 && cn < 0.2) ? 2
                                  : (cn > 0.4 && ce > 0.4) ? 3
                                                           : 0;

    // --- PID METRICS (L3 Tier: attributed via same common delta window) ---
    uint32_t ai = 0;
    uint32_t ptc = h->pid_tracker_count;
    if (ptc > MAX_ACTIVE_PIDS) ptc = MAX_ACTIVE_PIDS;

    float max_fps = 0;
    uint32_t max_gop = 0;
    bool is_initial = (h->live->total_ts_packets < 50000);

    for (uint32_t i = 0; i < ptc; i++) {
        uint16_t p = h->pid_active_list[i];
        if (p >= TS_PID_MAX) continue;
        uint64_t p_pd = h->live->pid_packet_count[p] - h->prev_snap_base->pid_packet_count[p];
        if (p_pd > 0) {
            uint64_t cb = (p_pd * TS_PACKET_BITS * NS_PER_SEC) / wall_dt;
            if (cb > TS_MAX_BITRATE_BPS) cb = TS_MAX_BITRATE_BPS;

            float alpha = is_initial ? 0.5f : 0.15f;
            if (h->live->pid_bitrate_bps[p] == 0)
                h->live->pid_bitrate_bps[p] = cb;
            else
                h->live->pid_bitrate_bps[p] = (uint64_t)(alpha * cb + (1.0f - alpha) * h->live->pid_bitrate_bps[p]);
        } else if (wall_dt >= 500000000ULL) {
            h->live->pid_bitrate_bps[p] = 0;
        }

        /* Calculate FPS based on frame counts detected since last snapshot */
        uint64_t cur_frames = h->pid_i_frames[p] + h->pid_p_frames[p] + h->pid_b_frames[p];
        uint64_t prev_frames = h->prev_snap_base_frames[p];
        if (cur_frames > prev_frames) {
            float inst_fps = (float)((cur_frames - prev_frames) * NS_PER_SEC) / wall_dt;
            if (h->pid_exact_fps[p] < 0.1) h->pid_exact_fps[p] = inst_fps;
            else h->pid_exact_fps[p] = 0.1f * inst_fps + 0.9f * h->pid_exact_fps[p];
        } else if (wall_dt >= 2000000000ULL) {
            h->pid_exact_fps[p] = 0;
        }
        h->prev_snap_base_frames[p] = cur_frames;

        const char* p_st = tsa_get_pid_type_name(h, p);
        /* If we have frame data, it's a video stream regardless of what PMT says */
        bool has_frames = (cur_frames > 0);
        if (strcmp(p_st, "H.264") == 0 || strcmp(p_st, "HEVC") == 0 || strcmp(p_st, "MPEG2-V") == 0 ||
            (has_frames && h->pid_exact_fps[p] > 0.5)) {
            if (h->pid_exact_fps[p] > max_fps) max_fps = h->pid_exact_fps[p];
            if (h->pid_gop_max[p] > max_gop) max_gop = h->pid_gop_max[p];
        }

        sn->pids[ai].pid = p;
        strncpy(sn->pids[ai].type_str, p_st, sizeof(sn->pids[ai].type_str) - 1);
        sn->pids[ai].type_str[sizeof(sn->pids[ai].type_str) - 1] = '\0';
        sn->pids[ai].bitrate_q16_16 = (int64_t)h->live->pid_bitrate_bps[p] << 16;
        sn->pids[ai].cc_errors = h->live->pid_cc_errors[p];
        sn->pids[ai].scrambled_packets = h->live->pid_scrambled_packets[p];
        sn->pids[ai].pes_errors = h->live->pid_pes_errors[p];
        sn->pids[ai].status = (uint8_t)h->pid_status[p];
        sn->pids[ai].width = h->pid_width[p];
        sn->pids[ai].height = h->pid_height[p];
        sn->pids[ai].profile = h->pid_profile[p];
        sn->pids[ai].level = h->pid_level[p];
        sn->pids[ai].chroma_format = h->pid_chroma_format[p];
        sn->pids[ai].bit_depth = h->pid_bit_depth[p];
        sn->pids[ai].exact_fps = h->pid_exact_fps[p];
        sn->pids[ai].audio_sample_rate = h->pid_audio_sample_rate[p];
        sn->pids[ai].audio_channels = h->pid_audio_channels[p];
        sn->pids[ai].gop_n = h->pid_last_gop_n[p];
        sn->pids[ai].gop_min = h->pid_gop_min[p];
        sn->pids[ai].gop_max = h->pid_gop_max[p];
        sn->pids[ai].gop_ms = h->pid_gop_ms[p];
        sn->pids[ai].i_frames = h->pid_i_frames[p];
        sn->pids[ai].p_frames = h->pid_p_frames[p];
        sn->pids[ai].b_frames = h->pid_b_frames[p];
        sn->pids[ai].closed_gops = h->pid_closed_gops[p];
        sn->pids[ai].open_gops = h->pid_open_gops[p];
        sn->pids[ai].has_cea708 = h->pid_has_cea708[p];
        sn->pids[ai].has_scte35 = h->pid_is_scte35[p];
        sn->pids[ai].is_closed_gop = h->pid_closed_gop[p];
        sn->pids[ai].scte35_alignment_error_ns = h->scte35_alignment_error_ns[p];
        sn->pids[ai].eb_fill_pct = (float)((double)(h->pid_eb_fill_q64[p] >> 64) * 100.0 / 1200000.0);
        sn->pids[ai].width = h->pid_width[p];
        sn->pids[ai].height = h->pid_height[p];
        sn->pids[ai].profile = h->pid_profile[p];
        sn->pids[ai].level = h->pid_level[p];
        sn->pids[ai].chroma_format = h->pid_chroma_format[p];
        sn->pids[ai].bit_depth = h->pid_bit_depth[p];
        ai++;
    }

    h->live->video_fps = max_fps;
    h->live->gop_ms = max_gop;

    sn->summary.master_health = h->last_health_score;
    sn->summary.total_packets = h->live->total_ts_packets;
    sn->summary.signal_lock = h->signal_lock;
    sn->summary.physical_bitrate_bps = h->live->physical_bitrate_bps;
    strncpy(sn->network_name, h->network_name, sizeof(sn->network_name) - 1);
    sn->network_name[sizeof(sn->network_name) - 1] = '\0';
    strncpy(sn->service_name, h->service_name, sizeof(sn->service_name) - 1);
    sn->service_name[sizeof(sn->service_name) - 1] = '\0';
    strncpy(sn->provider_name, h->provider_name, sizeof(sn->provider_name) - 1);
    sn->provider_name[sizeof(sn->provider_name) - 1] = '\0';
    sn->active_pid_count = ai;
    sn->stats = *h->live;

    atomic_store_explicit(&h->double_buffer.active_idx, inactive_idx, memory_order_release);
    *h->prev_snap_base = *h->live;
    h->last_snap_ns = stc;
    h->last_snap_wall_ns = n;
    h->live->pcr_jitter_max_ns = 0;

    /* Check for alert resolutions (Edge-triggered stabilization) */
    tsa_alert_check_resolutions(h);
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
