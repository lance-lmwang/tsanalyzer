#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <stdalign.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include "tsa_descriptors.h"
#include "tsa_internal.h"
#include "tsa_plugin.h"
#include "tsa_simd.h"

#define TAG "CORE"

#define ALLOC_OR_GOTO(p, type, count)    \
    do {                                 \
        p = calloc(count, sizeof(type)); \
        if (!p) goto fail;               \
    } while (0)

#define FREE_IF(p) \
    if (p) {       \
        free(p);   \
        p = NULL;  \
    }

tsa_handle_t* tsa_create(const tsa_config_t* cfg) {
    tsa_descriptors_init();
    tsa_handle_t* h = calloc(1, sizeof(tsa_handle_t));
    if (!h) return NULL;

    tsa_stream_init(&h->root_stream, h, NULL);
    ALLOC_OR_GOTO(h->pid_filters, ts_section_filter_t, TS_PID_MAX);
    ALLOC_OR_GOTO(h->pid_cc_error_suppression, uint32_t, TS_PID_MAX);

    h->pcr_tracks = calloc(TS_PID_MAX, sizeof(tsa_pcr_track_t));
    if (!h->pcr_tracks) goto fail;
    h->es_tracks = calloc(TS_PID_MAX, sizeof(tsa_es_track_t));
    if (!h->es_tracks) goto fail;

    ALLOC_OR_GOTO(h->pid_seen, bool, TS_PID_MAX);
    ALLOC_OR_GOTO(h->pid_is_pmt, bool, TS_PID_MAX);
    ALLOC_OR_GOTO(h->pid_is_scte35, bool, TS_PID_MAX);
    ALLOC_OR_GOTO(h->prev_snap_base_frames, uint64_t, TS_PID_MAX);

    h->pid_labels = calloc(TS_PID_MAX, sizeof(*h->pid_labels));
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

    if (h->config.webhook_url[0]) {
        h->webhook = tsa_webhook_init(h->config.webhook_url);
    }

    if (h->config.analysis.pcr_ema_alpha <= 0) h->config.analysis.pcr_ema_alpha = 0.05;
    h->pcr_ema_alpha_q32 = TO_Q32_32(h->config.analysis.pcr_ema_alpha);
    ts_pcr_window_init(&h->pcr_window, 128);
    ts_pcr_window_init(&h->pcr_long_window, 1024);

    /* Professional Packet Pool Initialization */
    h->pkt_pool = tsa_packet_pool_create(16384);
    if (!h->pkt_pool) goto fail;

    /* General Memory Pool for non-TS structures (PES buffers, etc.) */
    h->pool_size = 1024 * 1024 + 32 * 65536;
    if (posix_memalign(&h->pool_base, 64, h->pool_size) != 0) {
        h->pool_base = malloc(h->pool_size);
    }
    h->pool_offset = 0;

    for (int i = 0; i < TS_PID_MAX; i++) {
        tsa_pcr_track_init(&h->pcr_tracks[i], i, 0);

        tsa_es_track_t* es = &h->es_tracks[i];
        es->pid = i;
        es->video.gop_min = 0xFFFFFFFF;
        es->last_cc = 0x10;
        es->pes.last_pts_33 = 0x1FFFFFFFFULL;
        es->scte35.target_pts = 0xFFFFFFFFFFFFFFFFULL;
        h->pid_to_active_idx[i] = -1;
    }
    h->pid_tracker_count = 0;
    h->op_mode = cfg ? cfg->op_mode : TSA_MODE_LIVE;
    h->last_health_score = 100.0f;
    h->last_pcr_ticks = INVALID_PCR;
    h->master_pcr_pid = 0x1FFF;
    h->stc_slope_q64 = ((int128_t)1 << 64);

    if (!h->engine_started) {
        h->phys_stats.window_start_ns = 0;
        h->phys_stats.last_snap_bytes = 0;
        h->phys_stats.last_bps = 0;
    }

    /* Initialize Plugin Registry and Attach Builtin Plugins */
    tsa_plugins_init_registry();
    tsa_plugins_attach_builtin(h);

    tsa_stream_model_init(&h->ts_model);
    return h;

fail:
    tsa_destroy(h);
    return NULL;
}

void tsa_destroy_engines(tsa_handle_t* h) {
    if (!h) return;
    for (int i = 0; i < h->plugin_count; i++) {
        if (h->plugins[i].in_use && h->plugins[i].ops && h->plugins[i].ops->destroy) {
            h->plugins[i].ops->destroy(h->plugins[i].instance);
        }
        h->plugins[i].in_use = false;
    }
    h->plugin_count = 0;
}

void tsa_destroy(tsa_handle_t* h) {
    if (!h) return;
    tsa_plugins_destroy_all(h);
    ts_pcr_window_destroy(&h->pcr_window);
    ts_pcr_window_destroy(&h->pcr_long_window);
    if (h->pkt_pool) tsa_packet_pool_destroy(h->pkt_pool);
    if (h->pool_base) free(h->pool_base);

    FREE_IF(h->pid_filters);
    FREE_IF(h->pid_cc_error_suppression);
    if (h->pcr_tracks) {
        for (int i = 0; i < TS_PID_MAX; i++) tsa_pcr_track_destroy(&h->pcr_tracks[i]);
        free(h->pcr_tracks);
    }
    if (h->es_tracks) {
        free(h->es_tracks);
    }
    for (int i = 0; i < TS_PID_MAX; i++)
        if (h->pid_histograms[i]) free(h->pid_histograms[i]);
    FREE_IF(h->pid_seen);
    FREE_IF(h->pid_is_pmt);
    FREE_IF(h->pid_is_scte35);
    FREE_IF(h->prev_snap_base_frames);
    FREE_IF(h->pid_labels);
    FREE_IF(h->live);
    FREE_IF(h->prev_snap_base);
    FREE_IF(h->double_buffer.buffers[0]);
    FREE_IF(h->double_buffer.buffers[1]);
    FREE_IF(h->event_q);
    if (h->webhook) tsa_webhook_destroy(h->webhook);
    free(h);
}

static uint64_t tsa_recover_pts_64(tsa_handle_t* h, uint16_t pid, uint64_t pts_33) {
    tsa_es_track_t* es = &h->es_tracks[pid];
    if (es->pes.last_pts_33 == 0x1FFFFFFFFULL) {
        es->pes.last_pts_33 = pts_33;
        es->last_pts_extrapolated = pts_33;
        return pts_33;
    }
    uint64_t last_33 = es->pes.last_pts_33;
    uint64_t offset = 0;
    if (pts_33 < last_33 && (last_33 - pts_33) > 0x100000000ULL) offset = 0x200000000ULL;
    es->pes.last_pts_33 = pts_33;
    return pts_33 + offset;
}

void tsa_decode_packet_pure(tsa_handle_t* h, const uint8_t* p, uint64_t n, ts_decode_result_t* r) {
    (void)n;
    (void)h;
    r->pid = ((p[1] & 0x1F) << 8) | p[2];
    r->pusi = (p[1] & 0x40);
    r->af_len = (p[3] & 0x20) ? p[4] + 1 : 0;
    r->payload_len = 188 - 4 - r->af_len;
    r->has_payload = (p[3] & 0x10) && r->payload_len > 0;
    r->cc = p[3] & 0x0F;
    r->scrambled = (p[3] & 0xC0) != 0;
    r->has_discontinuity = (r->af_len > 1) && (p[5] & 0x80);
    r->has_pes_header = false;
    r->pts = r->dts = 0;
    if (r->pusi && r->has_payload) {
        const uint8_t* pay = p + 4 + r->af_len;
        if (r->payload_len >= 3 && pay[0] == 0x00 && pay[1] == 0x00 && pay[2] == 0x01) {
            tsa_pes_header_t ph;
            if (tsa_parse_pes_header(pay, r->payload_len, &ph) == 0) {
                r->has_pes_header = true;
                r->pts = ph.pts;
                r->dts = ph.dts;
            }
        } else if (r->payload_len >= 3) {
            if (h->live->pid_is_referenced[r->pid] && !h->pid_is_pmt[r->pid] && r->pid > 0x1F) {
                h->live->pid_pes_errors[r->pid]++;
                tsa_push_event(h, TSA_EVENT_PES_ERROR, r->pid, 0);
            }
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
    if (type != TSA_EVENT_SYNC_LOSS && !h->signal_lock) return;
    switch (type) {
        case TSA_EVENT_SYNC_LOSS:
            tsa_alert_update(h, TSA_ALERT_SYNC, true, "SYNC", pid);
            break;
        case TSA_EVENT_PAT_TIMEOUT:
            tsa_alert_update(h, TSA_ALERT_PAT, true, "PAT", pid);
            break;
        case TSA_EVENT_PMT_TIMEOUT:
            tsa_alert_update(h, TSA_ALERT_PMT, true, "PMT", pid);
            break;
        case TSA_EVENT_PID_ERROR:
            tsa_alert_update(h, TSA_ALERT_PID, true, "PID", pid);
            break;
        case TSA_EVENT_CC_ERROR:
            tsa_alert_update(h, TSA_ALERT_CC, true, "CC", pid);
            break;
        case TSA_EVENT_CRC_ERROR:
            tsa_alert_update(h, TSA_ALERT_CRC, true, "CRC", pid);
            break;
        case TSA_EVENT_TRANSPORT_ERROR:
            tsa_alert_update(h, TSA_ALERT_TRANSPORT, true, "TRANSPORT", pid);
            break;
        case TSA_EVENT_PTS_ERROR:
            tsa_alert_update(h, TSA_ALERT_PTS, true, "PTS", pid);
            break;
        case TSA_EVENT_PCR_REPETITION:
        case TSA_EVENT_PCR_JITTER:
            tsa_alert_update(h, TSA_ALERT_PCR, true, "PCR", pid);
            break;
        case TSA_EVENT_TSTD_UNDERFLOW:
            h->live->tstd_underflow.count++;
            tsa_alert_update(h, TSA_ALERT_TSTD, true, "TSTD", pid);
            break;
        case TSA_EVENT_TSTD_OVERFLOW:
            h->live->tstd_overflow.count++;
            tsa_alert_update(h, TSA_ALERT_TSTD, true, "TSTD", pid);
            break;
        case TSA_EVENT_ENTROPY_FREEZE:
            h->live->entropy_freeze.count++;
            tsa_alert_update(h, TSA_ALERT_ENTROPY, true, "ENTROPY", pid);
            break;
        case TSA_EVENT_SDT_TIMEOUT:
            h->live->sdt_timeout.count++;
            tsa_alert_update(h, TSA_ALERT_SDT, true, "SDT", pid);
            break;
        case TSA_EVENT_NIT_TIMEOUT:
            h->live->nit_timeout.count++;
            tsa_alert_update(h, TSA_ALERT_NIT, true, "NIT", pid);
            break;
        default:
            break;
    }
}

void tsa_process_packet(tsa_handle_t* h, const uint8_t* p, uint64_t n) {
    if (!h || !p) return;
    if (!h->engine_started) {
        h->start_ns = n;
        h->engine_started = true;
        h->last_snap_ns = n;
        h->last_snap_wall_ns = n;
        h->stc_ns = n;
        h->last_packet_rx_ns = n;
        h->phys_stats.window_start_ns = (h->config.op_mode == TSA_MODE_LIVE) ? (uint64_t)ts_now_ns128() : n;
    }
    if (h->last_packet_rx_ns != 0 && n > h->last_packet_rx_ns) {
        uint64_t delta_ns = n - h->last_packet_rx_ns;
        if (delta_ns < 1000000ULL)
            h->live->iat_hist.bucket_under_1ms++;
        else if (delta_ns < 2000000ULL)
            h->live->iat_hist.bucket_1_2ms++;
        else if (delta_ns < 5000000ULL)
            h->live->iat_hist.bucket_2_5ms++;
        else if (delta_ns < 10000000ULL)
            h->live->iat_hist.bucket_5_10ms++;
        else if (delta_ns < 100000000ULL)
            h->live->iat_hist.bucket_10_100ms++;
        else
            h->live->iat_hist.bucket_over_100ms++;
    }
    h->last_packet_rx_ns = n;
    if (p[0] != 0x47) {
        h->consecutive_sync_errors++;
        h->consecutive_good_syncs = 0;
        if (h->consecutive_sync_errors >= 5 && h->signal_lock) {
            h->live->sync_loss.count++;
            h->signal_lock = false;
            tsa_push_event(h, TSA_EVENT_SYNC_LOSS, 0, 0);
        }
        return;
    } else {
        h->consecutive_good_syncs++;
        h->consecutive_sync_errors = 0;
        if (h->consecutive_good_syncs >= 5 && !h->signal_lock) h->signal_lock = true;
    }
    if (h->op_mode == TSA_MODE_REPLAY) {
        double bpp = (double)TS_PACKET_BITS;
        uint64_t step = (uint64_t)(bpp * FROM_Q64_64(h->stc_slope_q64));
        if (step < 10000 || step > 1000000) {
            uint64_t tbr = h->config.forced_cbr_bitrate ? h->config.forced_cbr_bitrate : DEFAULT_REPLAY_BITRATE;
            step = (uint64_t)(bpp * NS_PER_SEC / tbr);
        }
        h->stc_ns += step;
    } else {
        if (h->stc_locked)
            h->stc_ns = (uint64_t)((h->stc_intercept_q64 + (int128_t)n * h->stc_slope_q64) >> 64);
        else
            h->stc_ns = n;
    }
    ts_decode_result_t r;
    tsa_decode_packet(h, p, n, &r);
    static __thread uint16_t lp = 0xFFFF;
    static __thread uint8_t lc = 0xFF;
    static __thread uint64_t ln = 0;
    bool is_dup = false;
    if (r.pid != 0x1FFF && r.pid == lp && r.cc == lc) {
        if (n > 0 && ln > 0 && (n - ln) < 1000ULL) is_dup = true;
    }
    lp = r.pid;
    lc = r.cc;
    ln = n;
    if (!is_dup) h->live->total_ts_packets++;
    if (h->config.analysis.enable_reactive_pid_filter && !tsa_stream_demux_check_pid(&h->root_stream, r.pid)) return;
    tsa_update_pid_tracker(h, r.pid);
    h->live->pid_packet_count[r.pid]++;
    if (r.pid < TS_PID_MAX) {
        if (!h->pid_histograms[r.pid]) h->pid_histograms[r.pid] = calloc(1, sizeof(tsa_histogram_t));
        if (h->pid_histograms[r.pid]) tsa_hist_add_packet(h->pid_histograms[r.pid], h->stc_ns, TS_PACKET_BITS);
    }
    if (r.scrambled) {
        h->live->pid_scrambled_packets[r.pid]++;
        tsa_push_event(h, TSA_EVENT_SCRAMBLED, r.pid, 0);
    }
    h->es_tracks[r.pid].last_seen_vstc = h->stc_ns;
    h->es_tracks[r.pid].last_seen_ns = n;
    h->pkts_since_pcr++;
    if (r.pid == 0 || r.pid == 0x10 || r.pid == 0x11 || h->pid_is_pmt[r.pid] || h->pid_is_scte35[r.pid])
        tsa_section_filter_push(h, r.pid, p, &r);
    h->current_ns = n;
    h->current_res = r;
    tsa_stream_send(&h->root_stream, p);
    if (h->pending_snapshot) {
        tsa_commit_snapshot(h, h->snapshot_stc);
        h->pending_snapshot = false;
    }
}

void tsa_update_srt_stats(tsa_handle_t* h, const tsa_srt_stats_t* s) {
    if (h && s) h->srt_live = *s;
}

void* tsa_mem_pool_alloc(tsa_handle_t* h, size_t sz) {
    if (!h || !h->pool_base) return NULL;
    size_t al = (h->pool_offset + 63) & ~63;
    if (al + sz > h->pool_size) return NULL;
    void* p = (uint8_t*)h->pool_base + al;
    h->pool_offset = al + ((sz > 64) ? sz : 64);
    return p;
}

float tsa_get_pid_tb_fill(tsa_handle_t* h, uint16_t p) {
    return (float)(h->es_tracks[p].tstd.tb_fill_q64 >> 64);
}

float tsa_get_pid_mb_fill(tsa_handle_t* h, uint16_t p) {
    return (float)(h->es_tracks[p].tstd.mb_fill_q64 >> 64);
}

float tsa_get_pid_eb_fill(tsa_handle_t* h, uint16_t p) {
    return (float)(h->es_tracks[p].tstd.eb_fill_q64 >> 64);
}

void tsa_reset_latched_errors(tsa_handle_t* h) {
    if (h && h->live) h->live->latched_cc_error = 0;
}

void tsa_feed_data(tsa_handle_t* h, const uint8_t* data, size_t len, uint64_t now_ns) {
    if (!h || !data || len == 0) return;
    size_t processed = 0;
    while (processed < len) {
        if (h->sync_buffer_len > 0) {
            size_t needed = 188 - h->sync_buffer_len;
            size_t to_copy = (len - processed < needed) ? (len - processed) : needed;
            memcpy(h->sync_buffer + h->sync_buffer_len, data + processed, to_copy);
            h->sync_buffer_len += to_copy;
            processed += to_copy;
            if (h->sync_buffer_len < 188) return;
            if (h->sync_buffer[0] == 0x47) {
                if (h->sync_state == TS_SYNC_LOCKED)
                    tsa_process_packet(h, h->sync_buffer, now_ns);
                else if (++h->sync_confirm_count >= 5)
                    h->sync_state = TS_SYNC_LOCKED;
                h->sync_buffer_len = 0;
            } else {
                h->sync_state = TS_SYNC_HUNTING;
                h->signal_lock = false;
                memmove(h->sync_buffer, h->sync_buffer + 1, h->sync_buffer_len - 1);
                h->sync_buffer_len--;
                continue;
            }
        }
        while (processed + 188 <= len) {
            const uint8_t* p = data + processed;
            if (p[0] == 0x47) {
                if (h->sync_state == TS_SYNC_LOCKED) {
                    tsa_process_packet(h, p, now_ns);
                    processed += 188;
                } else {
                    memcpy(h->sync_buffer, p, 188);
                    h->sync_buffer_len = 188;
                    processed += 188;
                    break;
                }
            } else {
                h->sync_state = TS_SYNC_HUNTING;
                h->signal_lock = false;
                intptr_t next_sync = tsa_simd.find_sync(data + processed + 1, len - (processed + 1));
                if (next_sync >= 0)
                    processed += (size_t)next_sync + 1;
                else
                    processed = len;
            }
        }
        if (processed < len) {
            size_t rem = len - processed;
            memcpy(h->sync_buffer + h->sync_buffer_len, data + processed, rem);
            h->sync_buffer_len += rem;
            processed += rem;
        }
    }
}

void tsa_tstd_drain(tsa_handle_t* h, uint16_t pid) {
    if (!h || h->stc_ns == 0) return;
    tsa_es_track_t* es = &h->es_tracks[pid];
    while (es->au_q.head != es->au_q.tail) {
        if (h->stc_ns >= es->au_q.queue[es->au_q.head].dts_ns) {
            es->tstd.eb_fill_q64 -= INT_TO_Q64_64(es->au_q.queue[es->au_q.head].size);
            if (es->tstd.eb_fill_q64 < 0) es->tstd.eb_fill_q64 = 0;
            es->au_q.head = (es->au_q.head + 1) % TSA_AU_QUEUE_SIZE;
        } else
            break;
    }
}

void tsa_handle_internal_drop(tsa_handle_t* h, uint64_t drop_count) {
    if (!h) return;
    h->live->internal_analyzer_drop += drop_count;
    for (int i = 0; i < TS_PID_MAX; i++)
        if (h->pid_seen[i]) h->es_tracks[i].ignore_next_cc = true;
}
