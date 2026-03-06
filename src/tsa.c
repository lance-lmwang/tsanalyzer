#define _GNU_SOURCE
#include <math.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "tsa_internal.h"
#include "tsa_plugin.h"

/* --- Macros --- */
#define ALLOC_OR_GOTO(p, t, n)    \
    do {                          \
        p = calloc(n, sizeof(t)); \
        if (!(p)) goto fail;      \
    } while (0)

#define FREE_IF(p)      \
    do {                \
        if (p) free(p); \
    } while (0)

/* --- Core Management --- */

tsa_handle_t* tsa_create(const tsa_config_t* cfg) {
    tsa_handle_t* h = calloc(1, sizeof(tsa_handle_t));
    if (!h) return NULL;

    tsa_stream_init(&h->root_stream, h, NULL);
    ALLOC_OR_GOTO(h->pid_filters, ts_section_filter_t, TS_PID_MAX);
    ALLOC_OR_GOTO(h->pid_status, tsa_measurement_status_t, TS_PID_MAX);
    ALLOC_OR_GOTO(h->pid_cc_error_suppression, uint32_t, TS_PID_MAX);
    ALLOC_OR_GOTO(h->clock_inspectors, tsa_clock_inspector_t, TS_PID_MAX);
    ALLOC_OR_GOTO(h->pid_eb_fill_q64, int128_t, TS_PID_MAX);
    ALLOC_OR_GOTO(h->pid_tb_fill_q64, int128_t, TS_PID_MAX);
    ALLOC_OR_GOTO(h->pid_mb_fill_q64, int128_t, TS_PID_MAX);
    ALLOC_OR_GOTO(h->last_buffer_leak_vstc, uint64_t, TS_PID_MAX);
    ALLOC_OR_GOTO(h->pid_bitrate_ema, double, TS_PID_MAX);
    ALLOC_OR_GOTO(h->pid_bitrate_min, uint64_t, TS_PID_MAX);
    ALLOC_OR_GOTO(h->pid_bitrate_max, uint64_t, TS_PID_MAX);
    ALLOC_OR_GOTO(h->last_cc, uint8_t, TS_PID_MAX);
    ALLOC_OR_GOTO(h->ignore_next_cc, bool, TS_PID_MAX);
    ALLOC_OR_GOTO(h->pid_seen, bool, TS_PID_MAX);
    ALLOC_OR_GOTO(h->pid_is_pmt, bool, TS_PID_MAX);
    ALLOC_OR_GOTO(h->pid_is_scte35, bool, TS_PID_MAX);
    ALLOC_OR_GOTO(h->pid_stream_type, uint8_t, TS_PID_MAX);
    ALLOC_OR_GOTO(h->pid_pes_buf, uint8_t*, TS_PID_MAX);
    ALLOC_OR_GOTO(h->pid_pes_len, uint32_t, TS_PID_MAX);
    ALLOC_OR_GOTO(h->pid_pes_cap, uint32_t, TS_PID_MAX);
    ALLOC_OR_GOTO(h->pid_width, uint16_t, TS_PID_MAX);
    ALLOC_OR_GOTO(h->pid_height, uint16_t, TS_PID_MAX);
    ALLOC_OR_GOTO(h->pid_profile, uint8_t, TS_PID_MAX);
    ALLOC_OR_GOTO(h->pid_level, uint8_t, TS_PID_MAX);
    ALLOC_OR_GOTO(h->pid_chroma_format, uint8_t, TS_PID_MAX);
    ALLOC_OR_GOTO(h->pid_bit_depth, uint8_t, TS_PID_MAX);
    ALLOC_OR_GOTO(h->pid_exact_fps, float, TS_PID_MAX);
    ALLOC_OR_GOTO(h->pid_audio_sample_rate, uint32_t, TS_PID_MAX);
    ALLOC_OR_GOTO(h->pid_audio_channels, uint8_t, TS_PID_MAX);
    ALLOC_OR_GOTO(h->pid_log2_max_frame_num, uint8_t, TS_PID_MAX);
    ALLOC_OR_GOTO(h->pid_last_frame_num, uint32_t, TS_PID_MAX);
    ALLOC_OR_GOTO(h->pid_frame_num_valid, bool, TS_PID_MAX);
    ALLOC_OR_GOTO(h->pid_gop_n, uint32_t, TS_PID_MAX);
    ALLOC_OR_GOTO(h->pid_last_gop_n, uint32_t, TS_PID_MAX);
    ALLOC_OR_GOTO(h->pid_gop_min, uint32_t, TS_PID_MAX);
    ALLOC_OR_GOTO(h->pid_gop_max, uint32_t, TS_PID_MAX);
    ALLOC_OR_GOTO(h->pid_last_idr_ns, uint64_t, TS_PID_MAX);
    ALLOC_OR_GOTO(h->pid_gop_ms, uint32_t, TS_PID_MAX);
    ALLOC_OR_GOTO(h->pid_i_frames, uint64_t, TS_PID_MAX);
    ALLOC_OR_GOTO(h->pid_p_frames, uint64_t, TS_PID_MAX);
    ALLOC_OR_GOTO(h->pid_b_frames, uint64_t, TS_PID_MAX);
    ALLOC_OR_GOTO(h->pid_has_cea708, bool, TS_PID_MAX);
    ALLOC_OR_GOTO(h->pid_last_pts_33, uint64_t, TS_PID_MAX);
    ALLOC_OR_GOTO(h->pid_pts_offset_64, uint64_t, TS_PID_MAX);
    ALLOC_OR_GOTO(h->pid_last_seen_vstc, uint64_t, TS_PID_MAX);
    ALLOC_OR_GOTO(h->pid_last_seen_ns, uint64_t, TS_PID_MAX);

    h->pid_labels = calloc(TS_PID_MAX, 128);
    h->pid_au_q = calloc(TS_PID_MAX, sizeof(*h->pid_au_q));
    h->pid_au_head = calloc(TS_PID_MAX, 1);
    h->pid_au_tail = calloc(TS_PID_MAX, 1);
    h->pid_pending_dts = calloc(TS_PID_MAX, sizeof(uint64_t));
    h->live = calloc(1, sizeof(tsa_tr101290_stats_t));
    h->prev_snap_base = calloc(1, sizeof(tsa_tr101290_stats_t));
    h->double_buffer.buffers[0] = calloc(1, sizeof(tsa_snapshot_full_t));
    h->double_buffer.buffers[1] = calloc(1, sizeof(tsa_snapshot_full_t));
    atomic_init(&h->double_buffer.active_idx, 0);

    h->event_q = calloc(1, sizeof(tsa_event_ring_t));
    if (h->event_q) {
        atomic_init(&h->event_q->head, 0);
        atomic_init(&h->event_q->tail, 0);
    }

    if (!h->live || !h->prev_snap_base || !h->double_buffer.buffers[0] || !h->double_buffer.buffers[1] || !h->event_q)
        goto fail;
    if (cfg) h->config = *cfg;
    if (h->config.pcr_ema_alpha <= 0) h->config.pcr_ema_alpha = 0.05;
    h->pcr_ema_alpha_q32 = TO_Q32_32(h->config.pcr_ema_alpha);
    ts_pcr_window_init(&h->pcr_window, 128);
    ts_pcr_window_init(&h->pcr_long_window, 1024);

    h->pool_size = 1024 * 1024 + 32 * 65536;
    if (posix_memalign(&h->pool_base, 64, h->pool_size) != 0) h->pool_base = malloc(h->pool_size);

    memset(h->network_name, 0, 256); memset(h->service_name, 0, 256); memset(h->provider_name, 0, 256);

    for (int i = 0; i < TS_PID_MAX; i++) {
        h->pid_to_active_idx[i] = -1;
        h->pid_gop_min[i] = 0xFFFFFFFF;
        h->pid_last_pts_33[i] = 0x1FFFFFFFFULL;
        h->clock_inspectors[i].pid = i;
        h->last_cc[i] = 0x10;
    }
    h->op_mode = cfg ? cfg->op_mode : TSA_MODE_LIVE;
    h->last_health_score = 100.0f;
    h->last_pcr_ticks = INVALID_PCR;
    h->stc_slope_q64 = ((int128_t)1 << 64);

    tsa_plugin_register(&tsa_scte35_engine);
    tsa_plugin_register(&tr101290_ops);
    tsa_plugin_register(&codec_ops);
    tsa_plugin_register(&pcr_ops);
    tsa_plugin_register(&essence_ops);

    tsa_plugin_attach_instance(h, &tsa_scte35_engine);
    tsa_register_codec_engine(h);
    tsa_register_tr101290_engine(h);
    tsa_register_pcr_engine(h);
    tsa_register_essence_engine(h);

    return h;
fail:
    tsa_destroy(h);
    return NULL;
}

void tsa_destroy(tsa_handle_t* h) {
    if (!h) return;
    for (int i = 0; i < h->plugin_count; i++) {
        if (h->plugins[i].ops->destroy) h->plugins[i].ops->destroy(h->plugins[i].instance);
    }
    ts_pcr_window_destroy(&h->pcr_window);
    ts_pcr_window_destroy(&h->pcr_long_window);
    if (h->pool_base) free(h->pool_base);
    FREE_IF(h->pid_filters); FREE_IF(h->pid_pes_buf);
    FREE_IF(h->pid_status); FREE_IF(h->pid_cc_error_suppression);
    FREE_IF(h->clock_inspectors); FREE_IF(h->pid_eb_fill_q64);
    FREE_IF(h->pid_tb_fill_q64); FREE_IF(h->pid_mb_fill_q64);
    FREE_IF(h->last_buffer_leak_vstc); FREE_IF(h->pid_bitrate_ema);
    FREE_IF(h->pid_bitrate_min); FREE_IF(h->pid_bitrate_max);
    FREE_IF(h->last_cc); FREE_IF(h->ignore_next_cc);
    FREE_IF(h->pid_seen); FREE_IF(h->pid_is_pmt);
    FREE_IF(h->pid_is_scte35); FREE_IF(h->pid_stream_type);
    FREE_IF(h->pid_pes_len); FREE_IF(h->pid_pes_cap);
    FREE_IF(h->pid_width); FREE_IF(h->pid_height);
    FREE_IF(h->pid_profile); FREE_IF(h->pid_level);
    FREE_IF(h->pid_chroma_format); FREE_IF(h->pid_bit_depth);
    FREE_IF(h->pid_exact_fps); FREE_IF(h->pid_audio_sample_rate);
    FREE_IF(h->pid_audio_channels); FREE_IF(h->pid_log2_max_frame_num);
    FREE_IF(h->pid_last_frame_num); FREE_IF(h->pid_frame_num_valid);
    FREE_IF(h->pid_gop_n); FREE_IF(h->pid_last_gop_n);
    FREE_IF(h->pid_gop_min); FREE_IF(h->pid_gop_max);
    FREE_IF(h->pid_last_idr_ns); FREE_IF(h->pid_gop_ms);
    FREE_IF(h->pid_i_frames); FREE_IF(h->pid_p_frames);
    FREE_IF(h->pid_b_frames); FREE_IF(h->pid_labels);
    FREE_IF(h->pid_au_q); FREE_IF(h->pid_au_head);
    FREE_IF(h->pid_au_tail); FREE_IF(h->pid_pending_dts);
    FREE_IF(h->pid_last_pts_33); FREE_IF(h->pid_pts_offset_64);
    FREE_IF(h->pid_last_seen_vstc); FREE_IF(h->pid_last_seen_ns);
    FREE_IF(h->pid_has_cea708); FREE_IF(h->prev_snap_base);
    FREE_IF(h->double_buffer.buffers[0]); FREE_IF(h->double_buffer.buffers[1]);
    FREE_IF(h->event_q);
    free(h);
}

void tsa_plugin_attach_instance(tsa_handle_t* h, tsa_plugin_ops_t* ops) {
    if (!h || !ops || h->plugin_count >= MAX_TSA_PLUGINS) return;
    void* instance = ops->create(h);
    if (!instance) return;
    h->plugins[h->plugin_count].ops = ops;
    h->plugins[h->plugin_count].instance = instance;
    if (ops->get_stream) {
        tsa_stream_t* child = ops->get_stream(instance);
        if (child) tsa_stream_attach(&h->root_stream, child);
    }
    h->plugin_count++;
}

static uint64_t tsa_recover_pts_64(tsa_handle_t* h, uint16_t pid, uint64_t pts_33) {
    if (h->pid_last_pts_33[pid] == 0x1FFFFFFFFULL) {
        h->pid_last_pts_33[pid] = pts_33;
        h->pid_pts_offset_64[pid] = 0;
        return pts_33;
    }
    uint64_t last_33 = h->pid_last_pts_33[pid];
    if (pts_33 < last_33 && (last_33 - pts_33) > 0x100000000ULL) h->pid_pts_offset_64[pid] += 0x200000000ULL;
    else if (pts_33 > last_33 && (pts_33 - last_33) > 0x100000000ULL) {
        if (h->pid_pts_offset_64[pid] >= 0x200000000ULL) h->pid_pts_offset_64[pid] -= 0x200000000ULL;
    }
    h->pid_last_pts_33[pid] = pts_33;
    return pts_33 + h->pid_pts_offset_64[pid];
}

void tsa_decode_packet_pure(tsa_handle_t* h, const uint8_t* p, uint64_t n, ts_decode_result_t* r) {
    (void)n; (void)h;
    r->pid = ((p[1] & 0x1F) << 8) | p[2];
    r->pusi = (p[1] & 0x40);
    r->af_len = (p[3] & 0x20) ? p[4] + 1 : 0;
    r->payload_len = 188 - 4 - r->af_len;
    r->has_payload = (p[3] & 0x10) && r->payload_len > 0;
    r->cc = p[3] & 0x0F;
    r->has_discontinuity = (r->af_len > 1) && (p[5] & 0x80);
    r->has_pes_header = false; r->pts = r->dts = 0;
    if (r->pusi && r->has_payload) {
        tsa_pes_header_t ph;
        if (tsa_parse_pes_header(p + 4 + r->af_len, r->payload_len, &ph) == 0) {
            r->has_pes_header = true; r->pts = ph.pts; r->dts = ph.dts;
        }
    }
}

void tsa_decode_packet(tsa_handle_t* h, const uint8_t* p, uint64_t n, ts_decode_result_t* r) {
    tsa_decode_packet_pure(h, p, n, r);
    if (r->has_pes_header) {
        r->pts = tsa_recover_pts_64(h, r->pid, r->pts);
        r->dts = tsa_recover_pts_64(h, r->pid, r->dts);
    }
}

void tsa_push_event(tsa_handle_t* h, tsa_event_type_t type, uint16_t pid, uint64_t val) {
    if (!h || !h->event_q) return;
    size_t head = atomic_load_explicit(&h->event_q->head, memory_order_relaxed);
    size_t tail = atomic_load_explicit(&h->event_q->tail, memory_order_acquire);
    if (head - tail >= MAX_EVENT_QUEUE) return;
    size_t idx = head % MAX_EVENT_QUEUE;
    h->event_q->events[idx].type = type;
    h->event_q->events[idx].pid = pid;
    h->event_q->events[idx].timestamp_ns = h->stc_ns;
    h->event_q->events[idx].value = val;
    atomic_store_explicit(&h->event_q->head, head + 1, memory_order_release);
}

void tsa_process_packet(tsa_handle_t* h, const uint8_t* p, uint64_t n) {
    if (!h || !p) return;
    if (!h->engine_started) {
        h->start_ns = n; h->engine_started = true; h->last_snap_ns = n;
        h->last_snap_wall_ns = n; h->last_pcr_arrival_ns = n; h->stc_ns = n;
        h->last_packet_rx_ns = n;
    }

    if (h->last_packet_rx_ns != 0 && n > h->last_packet_rx_ns) {
        uint64_t delta_ns = n - h->last_packet_rx_ns;
        if (delta_ns < 1000000ULL) h->live->iat_hist.bucket_under_1ms++;
        else if (delta_ns < 2000000ULL) h->live->iat_hist.bucket_1_2ms++;
        else if (delta_ns < 5000000ULL) h->live->iat_hist.bucket_2_5ms++;
        else if (delta_ns < 10000000ULL) h->live->iat_hist.bucket_5_10ms++;
        else if (delta_ns < 100000000ULL) h->live->iat_hist.bucket_10_100ms++;
        else h->live->iat_hist.bucket_over_100ms++;
    }
    h->last_packet_rx_ns = n;

    if (p[0] != 0x47) {
        h->consecutive_sync_errors++; h->consecutive_good_syncs = 0;
        if (h->consecutive_sync_errors >= 5 && h->signal_lock) {
            h->live->sync_loss.count++; h->signal_lock = false;
            tsa_push_event(h, TSA_EVENT_SYNC_LOSS, 0, 0);
        }
        return;
    } else {
        h->consecutive_good_syncs++; h->consecutive_sync_errors = 0;
        if (h->consecutive_good_syncs >= 2) h->signal_lock = true;
    }

    /* VIRTUAL STC UPDATE */
    if (h->op_mode == TSA_MODE_REPLAY) {
        /* In Replay mode, advance STC by packet steps to ensure determinism.
         * Priority: 1. Regressed slope, 2. Configured CBR, 3. 10Mbps fallback. */
        double bits_per_pkt = (double)TS_PACKET_BITS;
        uint64_t step_ns = (uint64_t)(bits_per_pkt * FROM_Q64_64(h->stc_slope_q64));

        /* If slope is uninitialized (1.0 in Q64 leads to step_ns ~TS_PACKET_BITS) or invalid,
         * calculate from bitrate. Default is 10Mbps. */
        if (step_ns < 10000 || step_ns > 1000000) {
            uint64_t target_br = h->config.forced_cbr_bitrate ? h->config.forced_cbr_bitrate : DEFAULT_REPLAY_BITRATE;
            step_ns = (uint64_t)(bits_per_pkt * NS_PER_SEC / target_br);
        }
        h->stc_ns += step_ns;

    } else {
        if (h->stc_locked) h->stc_ns = (uint64_t)((h->stc_intercept_q64 + (int128_t)n * h->stc_slope_q64) >> 64);
        else h->stc_ns = n;
    }

    ts_decode_result_t r;
    tsa_decode_packet(h, p, n, &r);
    if (h->config.enable_reactive_pid_filter && !tsa_stream_demux_check_pid(&h->root_stream, r.pid)) return;

    h->live->total_ts_packets++;
    tsa_update_pid_tracker(h, r.pid);
    h->live->pid_packet_count[r.pid]++;
    h->live->pid_last_seen_vstc[r.pid] = h->stc_ns;
    h->live->pid_last_seen_ns[r.pid] = n;
    h->pkts_since_pcr++;

    if (r.pid == 0 || r.pid == 0x10 || r.pid == 0x11 || h->pid_is_pmt[r.pid] || h->pid_is_scte35[r.pid])
        tsa_section_filter_push(h, r.pid, p, &r);

    h->current_ns = n; h->current_res = r;
    tsa_stream_send(&h->root_stream, p);
    if (h->pending_snapshot) { tsa_commit_snapshot(h, h->snapshot_stc); h->pending_snapshot = false; }
}

void tsa_update_srt_stats(tsa_handle_t* h, const tsa_srt_stats_t* s) { if (h && s) h->srt_live = *s; }
void* tsa_mem_pool_alloc(tsa_handle_t* h, size_t sz) {
    if (!h || !h->pool_base) return NULL;
    size_t al = (h->pool_offset + 63) & ~63;
    if (al + sz > h->pool_size) return NULL;
    void* p = (uint8_t*)h->pool_base + al;
    h->pool_offset = al + ((sz > 64) ? sz : 64);
    return p;
}
float tsa_get_pid_tb_fill(tsa_handle_t* h, uint16_t p) { return (float)(h->pid_tb_fill_q64[p] >> 64); }
float tsa_get_pid_mb_fill(tsa_handle_t* h, uint16_t p) { return (float)(h->pid_mb_fill_q64[p] >> 64); }
float tsa_get_pid_eb_fill(tsa_handle_t* h, uint16_t p) { return (float)(h->pid_eb_fill_q64[p] >> 64); }
void tsa_reset_latched_errors(tsa_handle_t* h) { if (h && h->live) h->live->latched_cc_error = 0; }

void tsa_feed_data(tsa_handle_t* h, const uint8_t* data, size_t len, uint64_t now_ns) {
    if (!h || !data || len == 0) return;
    size_t processed = 0;
    while (processed < len) {
        if (h->sync_buffer_len > 0) {
            size_t needed = 188 - h->sync_buffer_len;
            size_t to_copy = (len - processed < needed) ? (len - processed) : needed;
            memcpy(h->sync_buffer + h->sync_buffer_len, data + processed, to_copy);
            h->sync_buffer_len += to_copy; processed += to_copy;
            if (h->sync_buffer_len < 188) return;
            if (h->sync_buffer[0] == 0x47) {
                if (h->sync_state == TS_SYNC_LOCKED) tsa_process_packet(h, h->sync_buffer, now_ns);
                else if (++h->sync_confirm_count >= 5) { h->sync_state = TS_SYNC_LOCKED; h->signal_lock = true; }
                h->sync_buffer_len = 0;
            } else {
                h->sync_state = TS_SYNC_HUNTING; h->signal_lock = false;
                memmove(h->sync_buffer, h->sync_buffer + 1, h->sync_buffer_len - 1); h->sync_buffer_len--;
                continue;
            }
        }
        while (processed + 188 <= len) {
            const uint8_t* p = data + processed;
            if (p[0] == 0x47) {
                if (h->sync_state == TS_SYNC_LOCKED) { tsa_process_packet(h, p, now_ns); processed += 188; }
                else { memcpy(h->sync_buffer, p, 188); h->sync_buffer_len = 188; processed += 188; break; }
            } else { processed++; h->sync_state = TS_SYNC_HUNTING; h->signal_lock = false; }
        }
        if (processed < len) {
            size_t rem = len - processed;
            memcpy(h->sync_buffer + h->sync_buffer_len, data + processed, rem);
            h->sync_buffer_len += rem; processed += rem;
        }
    }
}

void tsa_tstd_drain(tsa_handle_t* h, uint16_t pid) {
    if (!h || h->stc_ns == 0) return;
    uint8_t head = h->pid_au_head[pid];
    while (head != h->pid_au_tail[pid]) {
        if (h->stc_ns >= h->pid_au_q[pid][head].dts_ns) {
            h->pid_eb_fill_q64[pid] -= INT_TO_Q64_64(h->pid_au_q[pid][head].size);
            if (h->pid_eb_fill_q64[pid] < 0) h->pid_eb_fill_q64[pid] = 0;
            head = (head + 1) % 32;
        } else break;
    }
    h->pid_au_head[pid] = head;
    h->live->pid_eb_fill_bytes[pid] = (uint32_t)(h->pid_eb_fill_q64[pid] >> 64);
}

void tsa_handle_internal_drop(tsa_handle_t* h, uint64_t drop_count) {
    if (!h) return;
    h->live->internal_analyzer_drop += drop_count;
    for (int i = 0; i < TS_PID_MAX; i++) if (h->pid_seen[i]) h->ignore_next_cc[i] = true;
}
