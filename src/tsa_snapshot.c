#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tsa_internal.h"

#define TAG "METROLOGY"

static void tsa_calc_stream_bitrate(tsa_handle_t* h, uint64_t n) {
    uint64_t curr_pkts = h->live->total_ts_packets;
    uint64_t now_metrology = (h->config.op_mode == TSA_MODE_LIVE) ? (uint64_t)ts_now_ns128() : n;

    if (h->phys_stats.window_start_ns == 0) {
        h->phys_stats.window_start_ns = now_metrology;
        h->phys_stats.last_snap_bytes = curr_pkts;
        return;
    }

    if (now_metrology <= h->phys_stats.window_start_ns) return;

    uint64_t dt = now_metrology - h->phys_stats.window_start_ns;
    uint64_t min_window = (h->config.op_mode == TSA_MODE_REPLAY) ? 100000000ULL : 500000000ULL;
    if (dt < min_window) return;

    uint64_t dp = (curr_pkts >= h->phys_stats.last_snap_bytes) ? (curr_pkts - h->phys_stats.last_snap_bytes) : 0;
    if (dp > 0) {
        uint64_t inst_bps = (uint64_t)(((unsigned __int128)dp * TS_PACKET_BITS * NS_PER_SEC) / dt);
        float alpha = (h->config.op_mode == TSA_MODE_REPLAY) ? 0.8f : 0.2f;
        if (h->phys_stats.last_bps == 0)
            h->phys_stats.last_bps = inst_bps;
        else
            h->phys_stats.last_bps = (uint64_t)(alpha * inst_bps + (1.0f - alpha) * h->phys_stats.last_bps);
    }
    h->phys_stats.last_snap_bytes = curr_pkts;
    h->phys_stats.window_start_ns = now_metrology;
    h->live->physical_bitrate_bps = h->phys_stats.last_bps;
}

static void tsa_eval_tr101290_alarms(tsa_handle_t* h, uint64_t n, uint64_t stc) {
    h->live->alarm_sync_loss = !h->signal_lock;
    h->live->alarm_cc_error = (h->live->cc_error.count > h->prev_snap_base->cc_error.count);

    uint64_t total_pcr_rep_err = 0;
    if (h->pcr_tracks) {
        for (int i = 0; i < TS_PID_MAX; i++) {
            if (h->pcr_tracks[i].initialized) {
                total_pcr_rep_err += h->pcr_tracks[i].priority_1_errors;
            }
        }
    }
    h->live->pcr_repetition_error.count = total_pcr_rep_err;

    if (!h->stc_locked || h->live->total_ts_packets < 1000) return;
    for (int i = 0; i < TS_PID_MAX; i++) {
        if (h->live->pid_is_referenced[i]) {
            uint64_t last_seen = h->es_tracks[i].last_seen_vstc;
            if (last_seen > 0 && (stc - last_seen) > TSA_TR101290_PID_TIMEOUT_NS) {
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
    uint8_t a = atomic_load_explicit(&h->double_buffer.active_idx, memory_order_acquire);
    uint8_t inactive_idx = a ^ 1;
    tsa_snapshot_full_t* sn = h->double_buffer.buffers[inactive_idx];

    tsa_calc_stream_bitrate(h, n);
    tsa_eval_tr101290_alarms(h, n, stc);

    h->live->pcr_bitrate_bps = h->live->physical_bitrate_bps;

    sn->summary.physical_bitrate_bps = h->live->physical_bitrate_bps;
    sn->summary.pcr_bitrate_bps = h->live->pcr_bitrate_bps;
    sn->summary.total_packets = h->live->total_ts_packets;
    sn->summary.signal_lock = h->signal_lock;
    sn->summary.active_pid_count = h->pid_tracker_count;
    sn->stats = *h->live;

    uint32_t ai = 0;
    uint32_t limit = (h->pid_tracker_count < MAX_ACTIVE_PIDS) ? h->pid_tracker_count : MAX_ACTIVE_PIDS;
    for (uint32_t i = 0; i < limit; i++) {
        uint16_t p = h->pid_active_list[i];
        if (p >= TS_PID_MAX) continue;
        tsa_es_track_t* es = &h->es_tracks[p];
        sn->pids[ai].pid = p;
        sn->pids[ai].cc_errors = h->live->pid_cc_errors[p];
        sn->pids[ai].width = es->video.width;
        sn->pids[ai].height = es->video.height;
        sn->pids[ai].exact_fps = es->video.exact_fps;
        sn->pids[ai].eb_fill_pct = (float)((double)(es->tstd.eb_fill_q64 >> 64) * 100.0 / 1200000.0);

        sn->pids[ai].i_frame_size_bytes = es->video.i_frame_size_bytes;
        sn->pids[ai].p_frame_size_bytes = es->video.p_frame_size_bytes;
        sn->pids[ai].b_frame_size_bytes = es->video.b_frame_size_bytes;
        memset(sn->pids[ai].gop_structure, 0, sizeof(sn->pids[ai].gop_structure));
        strncpy(sn->pids[ai].gop_structure, es->video.gop_structure, sizeof(sn->pids[ai].gop_structure) - 1);

        ai++;
    }
    sn->active_pid_count = ai;

    /* Professional A/V Skew Analysis (ISO/IEC 13818-1)
     * Calculate skew for the first program found with both video and audio. */
    h->live->av_sync_ms = 0;
    for (uint32_t i = 0; i < h->program_count; i++) {
        tsa_program_info_t* pr = &h->programs[i];
        uint16_t v_pid = 0, a_pid = 0;
        for (uint32_t j = 0; j < pr->stream_count; j++) {
            if (tsa_is_video(pr->streams[j].stream_type) && v_pid == 0) v_pid = pr->streams[j].pid;
            if (tsa_is_audio(pr->streams[j].stream_type) && a_pid == 0) a_pid = pr->streams[j].pid;
        }

        if (v_pid > 0 && a_pid > 0) {
            tsa_es_track_t* v_tk = &h->es_tracks[v_pid];
            tsa_es_track_t* a_tk = &h->es_tracks[a_pid];

            if (v_tk->last_pts_val > 0 && a_tk->last_pts_val > 0) {
                /* Formula: Skew = (PTS_a - VSTC_a) - (PTS_v - VSTC_v)
                 * All values normalized to 90kHz ticks for precision. */
                int64_t v_off = (int64_t)v_tk->last_pts_val - (int64_t)(v_tk->last_pts_vstc * 90 / 1000000);
                int64_t a_off = (int64_t)a_tk->last_pts_val - (int64_t)(a_tk->last_pts_vstc * 90 / 1000000);
                h->live->av_sync_ms = (int32_t)((a_off - v_off) / 90);
                break; /* Report for primary program */
            }
        }
    }
    sn->stats.av_sync_ms = h->live->av_sync_ms;

    atomic_store_explicit(&h->double_buffer.active_idx, inactive_idx, memory_order_release);
    *h->prev_snap_base = *h->live;
    h->last_snap_ns = stc;
    h->last_snap_wall_ns = n;
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
    return 0;
}
