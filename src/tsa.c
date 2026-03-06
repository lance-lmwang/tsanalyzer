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

/* --- Forward Declarations --- */
// moved to tsa_internal.h

/* --- High-precision Time Utilities --- */
int128_t ts_time_to_ns128(struct timespec ts) {
    return (int128_t)ts.tv_sec * 1000000000ULL + ts.tv_nsec;
}
struct timespec ns128_to_timespec(int128_t ns) {
    struct timespec ts;
    ts.tv_sec = (time_t)(ns / 1000000000ULL);
    ts.tv_nsec = (long)(ns % 1000000000ULL);
    return ts;
}
int128_t ts_now_ns128(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts_time_to_ns128(ts);
}

/* --- String Utilities --- */




void tsa_mbuf_init(tsa_metric_buffer_t* b, char* buf, size_t sz) {
    b->ptr = buf;
    b->size = sz;
    b->offset = 0;
    if (sz > 0) b->ptr[0] = '\0';
}

void tsa_mbuf_append_str(tsa_metric_buffer_t* b, const char* s) {
    size_t len = strlen(s);
    size_t rem = (b->offset < b->size) ? (b->size - b->offset) : 0;
    size_t cp = (len < rem) ? len : rem;
    if (cp > 0) {
        memcpy(b->ptr + b->offset, s, cp);
        b->offset += cp;
        if (b->offset < b->size) b->ptr[b->offset] = '\0';
    } else if (len > 0) {
        b->offset = b->size;
    }
}

void tsa_mbuf_append_int(tsa_metric_buffer_t* b, int64_t v) {
    char tmp[32];
    tsa_fast_itoa(tmp, v);
    tsa_mbuf_append_str(b, tmp);
}

void tsa_mbuf_append_float(tsa_metric_buffer_t* b, float v, int prec) {
    char tmp[32];
    tsa_fast_ftoa(tmp, v, prec);
    tsa_mbuf_append_str(b, tmp);
}

void tsa_mbuf_append_char(tsa_metric_buffer_t* b, char c) {
    if (b->offset < b->size) {
        b->ptr[b->offset++] = c;
        if (b->offset < b->size) b->ptr[b->offset] = '\0';
    }
}

const char* tsa_stream_type_to_str(uint8_t type) {
    switch (type) {
        case 0x01:
            return "MPEG1-V";
        case 0x02:
            return "MPEG2-V";
        case 0x03:
            return "MPEG1-A";
        case 0x04:
            return "MPEG2-A";
        case 0x06:
            return "Private";
        case 0x0f:
            return "ADTS-AAC";
        case 0x11:
            return "AAC-LATM";
        case 0x86:
            return "SCTE-35";
        case 0x24:
            return "HEVC";
        case 0x81:
            return "AC3";
        default:
            return "Unknown";
    }
}

/* --- Bit Reader moved to tsa_internal.h --- */

typedef struct {
    uint8_t stream_id;
    bool has_pts;
    bool has_dts;
    uint64_t pts;
    uint64_t dts;
    int header_len;
} tsa_pes_header_t;

static int tsa_parse_pes_header(const uint8_t* p, int len, tsa_pes_header_t* h) {
    if (len < 6) return -1;
    if (p[0] != 0 || p[1] != 0 || p[2] != 1) return -1;
    h->stream_id = p[3];
    if (h->stream_id == 0xBC || h->stream_id == 0xBE || h->stream_id == 0xBF || h->stream_id == 0xF0 ||
        h->stream_id == 0xF1 || h->stream_id == 0xFF || h->stream_id == 0xF2 || h->stream_id == 0xF8) {
        h->has_pts = h->has_dts = false;
        h->header_len = 6;
        return 0;
    }
    if (len < 9) return -1;
    uint8_t flags = p[7];
    h->has_pts = (flags & 0x80);
    h->has_dts = (flags & 0x40);
    h->header_len = 9 + p[8];
    if (h->has_pts) {
        if (len < 14) return -1;
        h->pts = ((uint64_t)(p[9] & 0x0E) << 29) | ((uint64_t)p[10] << 22) | ((uint64_t)(p[11] & 0xFE) << 14) |
                 ((uint64_t)p[12] << 7) | ((uint64_t)p[13] >> 1);
        h->dts = h->pts;
        if (h->has_dts) {
            if (len < 19) return -1;
            h->dts = ((uint64_t)(p[14] & 0x0E) << 29) | ((uint64_t)p[15] << 22) | ((uint64_t)(p[16] & 0xFE) << 14) |
                     ((uint64_t)p[17] << 7) | ((uint64_t)p[18] >> 1);
        }
    }
    return 0;
}

/* --- ES Parsing moved to tsa_es.c --- */

/* --- PSI/SI Parsing moved to tsa_psi.c --- */

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
    ALLOC_OR_GOTO(h->pid_to_active_idx, int16_t, TS_PID_MAX);
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
    h->pid_labels = calloc(TS_PID_MAX, 128);
    h->pid_au_q = calloc(TS_PID_MAX, sizeof(*h->pid_au_q));
    h->pid_au_head = calloc(TS_PID_MAX, 1);
    h->pid_au_tail = calloc(TS_PID_MAX, 1);
    h->op_mode = cfg ? cfg->op_mode : TSA_MODE_LIVE;
    h->pid_pending_dts = calloc(TS_PID_MAX, sizeof(uint64_t));
    h->pid_last_pts_33 = calloc(TS_PID_MAX, sizeof(uint64_t));
    h->pid_pts_offset_64 = calloc(TS_PID_MAX, sizeof(uint64_t));
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
    h->last_long_pcr_sample_ns = 0;
    h->long_term_drift_ppm = 0.0;
    h->stc_wall_drift_ppm = 0.0;
    h->pool_size = 1024 * 1024 + 32 * 65536; /* 1MB + 32 slots * 64KB for PES */

    if (posix_memalign(&h->pool_base, 64, h->pool_size) != 0) h->pool_base = malloc(h->pool_size);
    h->pool_offset = 0;
    h->pes_total_allocated = 32 * 4096;
    h->pes_max_quota = 64 * 1024 * 1024;
    h->last_trigger_reason = -1;
    for (int i = 0; i < TS_PID_MAX; i++) {
        h->pid_to_active_idx[i] = -1;
        h->pid_gop_min[i] = 0xFFFFFFFF;
        h->pid_pes_buf[i] = NULL;
        h->pid_pes_cap[i] = 0;
        h->pid_last_pts_33[i] = 0x1FFFFFFFFULL;
        h->clock_inspectors[i].pid = i;
    }
    /* Assign first 32 potential PES buffers to a pool */
    h->pes_pool_used = 0;
    h->op_mode = cfg ? cfg->op_mode : TSA_MODE_LIVE;
    
    /* Register default engines */
    extern tsa_plugin_ops_t tsa_scte35_engine;
    tsa_plugin_attach_instance(h, &tsa_scte35_engine);

    return h;
fail:
    tsa_destroy(h);
    return NULL;
}

void tsa_destroy(tsa_handle_t* h) {
    if (!h) return;
    tsa_destroy_engines(h);
    ts_pcr_window_destroy(&h->pcr_window);
    ts_pcr_window_destroy(&h->pcr_long_window);
    if (h->pool_base) free(h->pool_base);
    FREE_IF(h->pid_filters);
    FREE_IF(h->pid_pes_buf);
    FREE_IF(h->pid_status);
    FREE_IF(h->pid_cc_error_suppression);
    FREE_IF(h->clock_inspectors);
    FREE_IF(h->pid_eb_fill_q64);
    FREE_IF(h->pid_tb_fill_q64);
    FREE_IF(h->pid_mb_fill_q64);
    FREE_IF(h->last_buffer_leak_vstc);
    FREE_IF(h->pid_bitrate_ema);
    FREE_IF(h->pid_bitrate_min);
    FREE_IF(h->pid_bitrate_max);
    FREE_IF(h->last_cc);
    FREE_IF(h->ignore_next_cc);
    FREE_IF(h->pid_seen);
    FREE_IF(h->pid_is_pmt);
    FREE_IF(h->pid_is_scte35);
    FREE_IF(h->pid_stream_type);
    FREE_IF(h->pid_to_active_idx);
    FREE_IF(h->pid_pes_len);
    FREE_IF(h->pid_pes_cap);
    FREE_IF(h->pid_width);
    FREE_IF(h->pid_height);
    FREE_IF(h->pid_profile);
    FREE_IF(h->pid_level);
    FREE_IF(h->pid_chroma_format);
    FREE_IF(h->pid_bit_depth);
    FREE_IF(h->pid_exact_fps);
    FREE_IF(h->pid_audio_sample_rate);
    FREE_IF(h->pid_audio_channels);
    FREE_IF(h->pid_log2_max_frame_num);
    FREE_IF(h->pid_last_frame_num);
    FREE_IF(h->pid_frame_num_valid);
    FREE_IF(h->pid_gop_n);
    FREE_IF(h->pid_last_gop_n);
    FREE_IF(h->pid_gop_min);
    FREE_IF(h->pid_gop_max);
    FREE_IF(h->pid_last_idr_ns);
    FREE_IF(h->pid_gop_ms);
    FREE_IF(h->pid_i_frames);
    FREE_IF(h->pid_p_frames);
    FREE_IF(h->pid_b_frames);
    FREE_IF(h->pid_labels);
    FREE_IF(h->pid_au_q);
    FREE_IF(h->pid_au_head);
    FREE_IF(h->pid_au_tail);
    FREE_IF(h->pid_pending_dts);
    FREE_IF(h->pid_last_pts_33);
    FREE_IF(h->pid_pts_offset_64);
    FREE_IF(h->pid_has_cea708);
    FREE_IF(h->prev_snap_base);
    FREE_IF(h->double_buffer.buffers[0]);
    FREE_IF(h->double_buffer.buffers[1]);
    FREE_IF(h->event_q);
    free(h);
}

void tsa_precompile_pid_labels(tsa_handle_t* h, uint16_t pid) {
    if (!h || pid >= TS_PID_MAX) return;
    const char* codec = tsa_get_pid_type_name(h, pid);
    const char* type = "Other";
    if (strcmp(codec, "H.264") == 0 || strcmp(codec, "HEVC") == 0 || strcmp(codec, "MPEG2-V") == 0) {
        type = "Video";
    } else if (strcmp(codec, "AAC") == 0 || strcmp(codec, "ADTS-AAC") == 0 || strcmp(codec, "MPEG1-A") == 0 ||
               strcmp(codec, "MPEG2-A") == 0 || strcmp(codec, "AC3") == 0) {
        type = "Audio";
    }
    snprintf(h->pid_labels[pid], 128, "{stream_id=\"%s\",pid=\"0x%04x\",type=\"%s\",codec=\"%s\"}",
             h->config.input_label[0] ? h->config.input_label : "unknown", pid, type, codec);
}

void tsa_reset_pid_stats(tsa_handle_t* h, uint16_t pid) {
    h->pid_seen[pid] = false;
    h->live->pid_packet_count[pid] = 0;
    h->live->pid_bitrate_bps[pid] = 0;
    h->live->pid_cc_errors[pid] = 0;
    h->pid_bitrate_min[pid] = 0;
    h->pid_bitrate_max[pid] = 0;
    h->ignore_next_cc[pid] = false;
    h->pid_width[pid] = 0;
    h->pid_height[pid] = 0;
    h->pid_profile[pid] = 0;
    h->pid_audio_sample_rate[pid] = 0;
    h->pid_audio_channels[pid] = 0;
    h->pid_gop_n[pid] = 0;
    h->pid_gop_min[pid] = 0xFFFFFFFF;
    h->pid_gop_max[pid] = 0;
    h->pid_gop_ms[pid] = 0;
    h->pid_last_idr_ns[pid] = 0;
    h->pid_frame_num_valid[pid] = false;
    if (h->pid_pes_buf[pid]) h->pid_pes_len[pid] = 0;
    h->pid_eb_fill_q64[pid] = 0;
    h->pid_tb_fill_q64[pid] = 0;
    h->pid_mb_fill_q64[pid] = 0;
    h->live->pid_eb_fill_bytes[pid] = 0;
    h->live->pid_eb_fill_pct[pid] = 0;
    h->last_buffer_leak_vstc[pid] = 0;
    h->pid_status[pid] = TSA_STATUS_VALID;
}

int16_t tsa_update_pid_tracker(tsa_handle_t* h, uint16_t p) {
    int16_t idx = h->pid_to_active_idx[p];
    if (idx == -1) {
        if (h->pid_tracker_count < MAX_ACTIVE_PIDS) {
            h->pid_active_list[h->pid_tracker_count] = p;
            h->pid_to_active_idx[p] = h->pid_tracker_count;
            idx = h->pid_tracker_count++;
        } else {
            int ev = -1;
            for (int i = 0; i < MAX_ACTIVE_PIDS; i++) {
                bool prot = false;
                uint16_t c = h->pid_active_list[i];
                if (c <= 1 || h->pid_is_pmt[c])
                    prot = true;
                else {
                    for (int j = 0; j < 16; j++)
                        if (h->config.protected_pids[j] == c) {
                            prot = true;
                            break;
                        }
                }
                if (!prot) {
                    ev = i;
                    break;
                }
            }
            if (ev == -1) ev = 0;
            uint16_t ep = h->pid_active_list[ev];
            h->pid_to_active_idx[ep] = -1;
            tsa_reset_pid_stats(h, ep);
            for (int i = ev; i < MAX_ACTIVE_PIDS - 1; i++) {
                h->pid_active_list[i] = h->pid_active_list[i + 1];
                h->pid_to_active_idx[h->pid_active_list[i]] = i;
            }
            h->pid_active_list[MAX_ACTIVE_PIDS - 1] = p;
            h->pid_to_active_idx[p] = MAX_ACTIVE_PIDS - 1;
            idx = MAX_ACTIVE_PIDS - 1;
        }
        h->pid_seen[p] = true;
    }
    if (idx != (int16_t)h->pid_tracker_count - 1) {
        uint16_t cur = h->pid_active_list[idx];
        for (uint32_t i = (uint32_t)idx; i < h->pid_tracker_count - 1; i++) {
            h->pid_active_list[i] = h->pid_active_list[i + 1];
            h->pid_to_active_idx[h->pid_active_list[i]] = i;
        }
        h->pid_active_list[h->pid_tracker_count - 1] = cur;
        h->pid_to_active_idx[cur] = h->pid_tracker_count - 1;
        idx = h->pid_tracker_count - 1;
    }
    return idx;
}

static uint64_t tsa_recover_pts_64(tsa_handle_t* h, uint16_t pid, uint64_t pts_33) {
    if (h->pid_last_pts_33[pid] == 0x1FFFFFFFFULL) {
        h->pid_last_pts_33[pid] = pts_33;
        h->pid_pts_offset_64[pid] = 0;
        return pts_33;
    }
    uint64_t last_33 = h->pid_last_pts_33[pid];
    // PTS wrap: 33 bits = 0x1FFFFFFFF (8589934591)
    if (pts_33 < last_33 && (last_33 - pts_33) > 0x100000000ULL) {
        h->pid_pts_offset_64[pid] += 0x200000000ULL;
    } else if (pts_33 > last_33 && (pts_33 - last_33) > 0x100000000ULL) {
        if (h->pid_pts_offset_64[pid] >= 0x200000000ULL) {
            h->pid_pts_offset_64[pid] -= 0x200000000ULL;
        }
    }
    h->pid_last_pts_33[pid] = pts_33;
    return pts_33 + h->pid_pts_offset_64[pid];
}

void tsa_decode_packet_pure(tsa_handle_t* h, const uint8_t* p, uint64_t n, ts_decode_result_t* r) {
    if (!h || !p || !r) return;
    (void)n;
    r->pid = ((p[1] & 0x1F) << 8) | p[2];
    r->pusi = (p[1] & 0x40);
    r->af_len = (p[3] & 0x20) ? p[4] + 1 : 0;
    r->payload_len = 188 - 4 - r->af_len;
    r->has_payload = (p[3] & 0x10) && r->payload_len > 0;
    r->cc = p[3] & 0x0F;
    r->has_discontinuity = (r->af_len > 1) && (p[5] & 0x80);
    r->has_pes_header = false;
    r->pts = r->dts = 0;

    if (r->pusi && r->has_payload) {
        tsa_pes_header_t ph;
        if (tsa_parse_pes_header(p + 4 + r->af_len, r->payload_len, &ph) == 0) {
            r->has_pes_header = true;
            r->pts = ph.pts;
            r->dts = ph.dts;
        }
    }
}
void tsa_decode_packet(tsa_handle_t* h, const uint8_t* p, uint64_t n, ts_decode_result_t* r) {
    if (!h || !p || !r) return;
    tsa_decode_packet_pure(h, p, n, r);

    // Stateful PTS/DTS recovery
    if (r->has_pes_header) {
        r->pts = tsa_recover_pts_64(h, r->pid, r->pts);
        r->dts = tsa_recover_pts_64(h, r->pid, r->dts);
    }

    h->live->total_ts_packets++;
    tsa_update_pid_tracker(h, r->pid);
    h->live->pid_packet_count[r->pid]++;
    h->live->pid_last_seen_vstc[r->pid] = h->stc_ns;
    if (r->pid == 0 || r->pid == 0x10 || r->pid == 0x11 || h->pid_is_pmt[r->pid] || h->pid_is_scte35[r->pid]) {
        tsa_section_filter_push(h, r->pid, p, r);
    }
}

/* --- Section Filter moved to tsa_psi.c --- */

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

void tsa_handle_internal_drop(tsa_handle_t* h, uint64_t drop_count) {
    if (!h) return;
    h->live->internal_analyzer_drop += drop_count;
    for (int i = 0; i < TS_PID_MAX; i++) {
        if (h->pid_seen[i]) {
            h->ignore_next_cc[i] = true;
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
        } else {
            break;
        }
    }
    h->pid_au_head[pid] = head;
    h->live->pid_eb_fill_bytes[pid] = (uint32_t)(h->pid_eb_fill_q64[pid] >> 64);
}

#define TSA_MIN_PCR_BITRATE 10000
#define TSA_SYNC_GUARD_THRESHOLD_NS 1000000000ULL

void tsa_metrology_process(tsa_handle_t* h, const uint8_t* pkt, uint64_t now, const ts_decode_result_t* res) {
    if (!h || !pkt || !res) return;
    uint16_t pid = res->pid;
    h->pkts_since_pcr++;

    // Track every single PID seen for metrics
    tsa_update_pid_tracker(h, pid);

    if (now == h->metro_last_now) {
        uint64_t br = h->live->physical_bitrate_bps ? h->live->physical_bitrate_bps : 10000000;
        h->metro_offset += (1504ULL * 1000000000ULL) / br;
        now += h->metro_offset;
    } else {
        h->metro_last_now = now;
        h->metro_offset = 0;
    }

    // Calculate STC increment with sanity guards
    uint64_t br_for_inc = h->live->pcr_bitrate_bps;
    if (br_for_inc < 1000000) br_for_inc = h->live->physical_bitrate_bps;
    if (br_for_inc < 1000000) br_for_inc = 10000000;  // Default 10Mbps fallback

    uint64_t stc_step = (1504ULL * 1000000000ULL) / br_for_inc;
    if (stc_step > 1000000) stc_step = 1000000;  // Cap at 1ms per packet to prevent runaway

    if (h->stc_locked) {
        h->stc_ns += stc_step;
    } else {
        h->stc_ns += (now > h->last_pcr_arrival_ns) ? (now - h->last_pcr_arrival_ns) : 1000000ULL;
    }

    // --- Hybrid Sync Guard ---
    // If the gap between VSTC and System Clock exceeds threshold in Live mode,
    // snap back to system clock to prevent starvation or infinite drift.
    if (h->config.is_live) {
        uint64_t diff = (h->stc_ns > now) ? (h->stc_ns - now) : (now - h->stc_ns);
        if (diff > TSA_SYNC_GUARD_THRESHOLD_NS) {
            h->stc_ns = now;
        }
    }
    h->last_pcr_arrival_ns = now;

    if (h->live->pid_last_seen_ns[pid] > 0) {
        uint64_t dt = h->stc_ns - h->live->pid_last_seen_ns[pid];
        int128_t l_eb = TO_Q64_64(0.04), l_tb = TO_Q64_64(0.05);
        uint8_t st = h->pid_stream_type[pid];
        if (st == 0x1b) {
            l_eb = TO_Q64_64(0.04);
            l_tb = TO_Q64_64(0.05);
        } else if (st == 0x24) {
            l_eb = TO_Q64_64(0.06);
            l_tb = TO_Q64_64(0.08);
        } else if (st == 0x03 || st == 0x04 || st == 0x0f || st == 0x11 || st == 0x81) {
            l_eb = TO_Q64_64(0.002);
            l_tb = TO_Q64_64(0.004);
        }
        h->pid_tb_fill_q64[pid] -= (l_tb * dt);
        if (h->pid_tb_fill_q64[pid] < 0) h->pid_tb_fill_q64[pid] = 0;
        h->pid_mb_fill_q64[pid] -= (l_tb * dt);
        if (h->pid_mb_fill_q64[pid] < 0) h->pid_mb_fill_q64[pid] = 0;
        if (h->live->pid_is_referenced[pid]) {
            h->pid_eb_fill_q64[pid] -= (l_eb * dt);
            if (h->pid_eb_fill_q64[pid] < 0) h->pid_eb_fill_q64[pid] = 0;
        }
    }
    h->pid_tb_fill_q64[pid] += INT_TO_Q64_64(188);
    if (res->has_payload) {
        h->pid_mb_fill_q64[pid] += INT_TO_Q64_64(res->payload_len);
        if (h->live->pid_is_referenced[pid]) h->pid_eb_fill_q64[pid] += INT_TO_Q64_64(res->payload_len);
    }

    /* Drain EB based on DTS arrival */
    tsa_tstd_drain(h, pid);

    h->live->pid_last_seen_ns[pid] = h->stc_ns;
    if (pkt[1] & 0x80) {
        h->live->transport_error.count++;
        h->live->transport_error.last_timestamp_ns = now;
    }
    if (h->live->pid_packet_count[pid] > 1 && !res->has_discontinuity) {
        if (h->ignore_next_cc[pid]) {
            h->ignore_next_cc[pid] = false;
        } else {
            ts_cc_status_t s =
                cc_classify_error(h->last_cc[pid], res->cc, res->has_payload, (pkt[3] & 0x20) && !(pkt[3] & 0x10));
            if (s == TS_CC_LOSS) {
                h->pid_cc_error_suppression[pid]++;
                if (h->pid_cc_error_suppression[pid] >= 3) { // Threshold: 3 consecutive errors
                    if (h->live->cc_error.count == 0) h->live->cc_error.first_timestamp_ns = now;
                    h->live->cc_error.count++;
                    h->live->cc_error.last_timestamp_ns = now;
                    h->live->cc_error.triggering_vstc = h->stc_ns;
                    h->live->cc_error.absolute_byte_offset = h->live->total_ts_packets * 188;
                    h->live->pid_cc_errors[pid]++;
                    h->live->latched_cc_error = 1;
                    h->pid_status[pid] = TSA_STATUS_DEGRADED;
                    h->live->cc_loss_count += (res->cc - ((h->last_cc[pid] + 1) & 0x0F)) & 0x0F;
                    tsa_push_event(h, TSA_EVENT_CC_ERROR, pid, res->cc);
                }
            } else if (s == TS_CC_DUPLICATE) {
                h->live->cc_duplicate_count++;
            } else if (s == TS_CC_OUT_OF_ORDER) {
                if (h->live->cc_error.count == 0) h->live->cc_error.first_timestamp_ns = now;
                h->live->cc_error.count++;
                h->live->cc_error.last_timestamp_ns = now;
                h->pid_status[pid] = TSA_STATUS_DEGRADED;
                tsa_push_event(h, TSA_EVENT_CC_ERROR, pid, res->cc);
            } else {
                // Stable packets: Reset suppression counter after 100 good packets
                if (h->live->pid_packet_count[pid] % 100 == 0) {
                    h->pid_cc_error_suppression[pid] = 0;
                }
            }
        }
    }
    h->last_cc[pid] = res->cc;

    if ((pkt[3] & 0x20) && pkt[4] > 0 && (pkt[5] & 0x10)) {
        h->live->pid_is_referenced[pid] = true;
        tsa_update_pid_tracker(h, pid);

        // Update the new ClockInspector
        tsa_clock_update(pkt, &h->clock_inspectors[pid], now);
        
        // Sync new metrics to snapshot (for now, we'll use the ones from the first PCR PID seen)
        // In a real multi-program environment, we might want to aggregate or show per-PID.
        h->live->pcr_jitter_max_ns = (uint64_t)(fabs(h->clock_inspectors[pid].pcr_jitter_ms) * 1000000.0);
        h->live->pcr_repetition_max_ms = h->clock_inspectors[pid].pcr_interval_max_ticks / (PCR_TICKS_PER_MS);
        h->live->pcr_repetition_error.count = h->clock_inspectors[pid].priority_1_errors;

        uint64_t pt = extract_pcr(pkt), pn = pt * 1000 / 27;

        if (h->last_pcr_ticks > 0 && h->last_pcr_interval_bitrate_bps > 0) {
            uint64_t bits = h->pkts_since_pcr * 1504;
            uint64_t expected = h->last_pcr_ticks + (bits * 27000000 / h->last_pcr_interval_bitrate_bps);
            int64_t diff = (int64_t)pt - (int64_t)expected;
            if (diff < -((int64_t)1 << 41))
                diff += ((int64_t)1 << 42);
            else if (diff > ((int64_t)1 << 41))
                diff -= ((int64_t)1 << 42);
            h->live->pcr_accuracy_ns_piecewise = (double)diff * 1000.0 / 27.0;
        }

        if (h->config.op_mode == TSA_MODE_REPLAY || !h->stc_locked) {
            bool first_lock = !h->stc_locked;
            h->stc_ns = pn;
            h->stc_locked = true;
            if (first_lock) {
                h->last_snap_ns = pn;
                h->start_ns = pn;
                h->stc_first_lock_pcr_ns = pn;
                h->stc_first_lock_wall_ns = now;
                // Reset baseline for the next snapshot to avoid burst artifacts
                *h->prev_snap_base = *h->live;
                for (int i = 0; i < TS_PID_MAX; i++) {
                    if (h->pid_seen[i]) h->live->pid_last_seen_ns[i] = pn;
                }
            }
        }

        ts_pcr_window_add(&h->pcr_window, now, pn, 0);
        if (now - h->last_long_pcr_sample_ns >= 1000000000ULL) {
            ts_pcr_window_add(&h->pcr_long_window, now, pn, 0);
            h->last_long_pcr_sample_ns = now;
        }

        if (h->last_pcr_arrival_ns > 0) {
            int64_t dp = (int64_t)pt - (int64_t)h->last_pcr_ticks;
            if (dp < -((int64_t)1 << 41))
                dp += ((int64_t)1 << 42);
            else if (dp > ((int64_t)1 << 41))
                dp -= ((int64_t)1 << 42);

            if (dp > 0) {
                // VSTC PCR Bitrate
                unsigned __int128 b128 = (unsigned __int128)(h->pkts_since_pcr) * 1504 * 27000000;
                uint64_t br = (uint64_t)(b128 / dp);
                h->last_pcr_interval_bitrate_bps = br;
                h->live->last_pcr_interval_bitrate_bps = br;
                uint64_t al = (uint64_t)h->pcr_ema_alpha_q32;
                if (h->live->pcr_bitrate_bps == 0)
                    h->live->pcr_bitrate_bps = br;
                else
                    h->live->pcr_bitrate_bps = ((unsigned __int128)br * al +
                                                (unsigned __int128)h->live->pcr_bitrate_bps * ((1ULL << 32) - al)) >>
                                               32;
            }
        } else {
            h->stc_ns = pn;
        }
        h->last_pcr_ticks = pt;
        h->last_pcr_arrival_ns = now;
        h->pkts_since_pcr = 0;
    }
}

/* --- Engine Management --- */

void tsa_plugin_attach_instance(tsa_handle_t* h, tsa_plugin_ops_t* ops) {
    if (h->plugin_count >= MAX_TSA_PLUGINS) return;
    h->plugins[h->plugin_count].ops = ops;
    h->plugins[h->plugin_count].instance = ops->create(h);
    
    // Attach stream to root if plugin exposes one
    if (ops->get_stream) {
        tsa_stream_t* child_stream = ops->get_stream(h->plugins[h->plugin_count].instance);
        if (child_stream) {
            tsa_stream_attach(&h->root_stream, child_stream);
        }
    }
    
    h->plugin_count++;
}

void tsa_destroy_engines(tsa_handle_t* h) {
    for (int i = 0; i < h->plugin_count; i++) {
        if (h->plugins[i].ops->destroy) {
            h->plugins[i].ops->destroy(h->plugins[i].instance);
        }
    }
    h->plugin_count = 0;
}

void tsa_process_packet(tsa_handle_t* h, const uint8_t* p, uint64_t n) {
    if (!h || !p) return;
    uint64_t start = ts_now_ns128();

    if (!h->engine_started) {
        h->start_ns = n;
        h->engine_started = true;
        h->last_snap_ns = n;
        h->last_pcr_arrival_ns = n;
        h->stc_ns = n;
        h->last_pat_ns = n;
        h->last_pmt_ns = n;
    }
    if (p[0] != 0x47) {
        h->consecutive_sync_errors++;
        h->consecutive_good_syncs = 0;
        if (h->consecutive_sync_errors >= 5 && h->signal_lock) {
            h->live->sync_loss.count++;
            h->live->sync_loss.last_timestamp_ns = n;
            h->signal_lock = false;
            tsa_push_event(h, TSA_EVENT_SYNC_LOSS, 0, 0);
        }
        h->live->sync_byte_error.count++;
        h->live->sync_byte_error.last_timestamp_ns = n;
        return;
    } else {
        h->consecutive_good_syncs++;
        h->consecutive_sync_errors = 0;
        if (h->consecutive_good_syncs >= 2) h->signal_lock = true;
    }
    if (h->stc_locked) {
        h->stc_ns = (uint64_t)((h->stc_intercept_q64 + (int128_t)n * h->stc_slope_q64) >> 64);
    } else {
        h->stc_ns = n;
    }

    uint16_t current_pid = ((p[1] & 0x1F) << 8) | p[2];
    if (h->config.enable_reactive_pid_filter && current_pid < TSA_MAX_PID) {
        if (!tsa_stream_demux_check_pid(&h->root_stream, current_pid)) {
            // Astra Reactive PID Drop: avoid further parsing, decoding, and plugin routing
            return;
        }
    }

    ts_decode_result_t r;
    tsa_decode_packet(h, p, n, &r);    tsa_metrology_process(h, p, n, &r);

    /* Dispatch to modular plugins via Stream Tree */
    h->current_ns = n;
    h->current_res = r;
    tsa_stream_send(&h->root_stream, p);

    h->live->engine_processing_latency_ns = (uint64_t)(ts_now_ns128() - start);

    /* Commit snapshot if requested (synchronized with packet processing) */
    if (h->pending_snapshot) {
        tsa_commit_snapshot(h, h->snapshot_stc);
        h->pending_snapshot = false;
    }
}



const char* tsa_get_pid_type_name(const tsa_handle_t* h, uint16_t p) {
    if (!h || p >= TS_PID_MAX) return "Unknown";
    if (p == 0) return "PAT";
    if (p == 0x1FFF) return "Stuffing";
    if (h->pid_is_pmt[p]) return "PMT";
    uint8_t ty = h->pid_stream_type[p];
    if (ty == 0) {
        for (uint32_t i = 0; i < h->program_count; i++) {
            for (uint32_t j = 0; j < h->programs[i].stream_count; j++)
                if (h->programs[i].streams[j].pid == p) {
                    ty = h->programs[i].streams[j].stream_type;
                    break;
                }
            if (ty != 0) break;
        }
    }
    return (ty != 0) ? tsa_stream_type_to_str(ty) : "Unknown";
}

ts_cc_status_t cc_classify_error(uint8_t l, uint8_t c, bool p, bool a) {
    if (!p || a) return TS_CC_OK;
    if (c == l) return TS_CC_DUPLICATE;
    if (c == ((l + 1) & 0xF)) return TS_CC_OK;
    return ((c - l) & 0xF) < 8 ? TS_CC_LOSS : TS_CC_OUT_OF_ORDER;
}
uint32_t tsa_crc32_check(const uint8_t* data, int len) {
    return mpegts_crc32(data, len);
}


uint64_t extract_pcr(const uint8_t* p) {
    if (!(p[3] & 0x20) || p[4] == 0 || !(p[5] & 0x10)) return INVALID_PCR;
    uint64_t b =
        ((uint64_t)p[6] << 25) | ((uint64_t)p[7] << 17) | ((uint64_t)p[8] << 9) | ((uint64_t)p[9] << 1) | (p[10] >> 7);
    return b * 300 + (((uint16_t)(p[10] & 0x01) << 8) | p[11]);
}
void ts_pcr_window_init(ts_pcr_window_t* w, uint32_t s) {
    w->samples = calloc(s, sizeof(ts_pcr_sample_t));
    w->size = s;
    w->count = 0;
    w->head = 0;
}
void ts_pcr_window_destroy(ts_pcr_window_t* w) {
    if (w->samples) free(w->samples);
}
void ts_pcr_window_add(ts_pcr_window_t* w, uint64_t s, uint64_t p, uint64_t o) {
    (void)o;
    w->samples[w->head].sys_ns = s;
    w->samples[w->head].pcr_ns = p;
    w->head = (w->head + 1) % w->size;
    if (w->count < w->size) w->count++;
}



void tsa_update_srt_stats(tsa_handle_t* h, const tsa_srt_stats_t* s) {
    if (!h || !s) return;
    h->srt_live = *s;
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
    return (float)(h->pid_tb_fill_q64[p] >> 64);
}
float tsa_get_pid_mb_fill(tsa_handle_t* h, uint16_t p) {
    return (float)(h->pid_mb_fill_q64[p] >> 64);
}
float tsa_get_pid_eb_fill(tsa_handle_t* h, uint16_t p) {
    return (float)(h->pid_eb_fill_q64[p] >> 64);
}

void tsa_reset_latched_errors(tsa_handle_t* h) {
    if (!h || !h->live) return;
    h->live->latched_cc_error = 0;
}
void tsa_forensic_generate_json(tsa_handle_t* h, char* b, size_t s) {
    if (!h || !b || s < 256) return;
    snprintf(b, s, "{\"event\":\"forensic_capture\",\"trigger_reason\":%d,\"total_packets\":%llu}",
             h->last_trigger_reason, (unsigned long long)h->live->total_ts_packets);
}

struct tsa_packet_ring {
    uint8_t* buffer;
    uint64_t* timestamps;
    size_t sz;
    _Atomic uint64_t head;
    _Atomic uint64_t tail;
};
tsa_packet_ring_t* tsa_packet_ring_create(size_t sz) {
    tsa_packet_ring_t* r = calloc(1, sizeof(struct tsa_packet_ring));
    if (!r) return NULL;
    r->sz = sz;
    r->buffer = malloc(sz * 188);
    r->timestamps = malloc(sz * sizeof(uint64_t));
    atomic_init(&r->head, 0);
    atomic_init(&r->tail, 0);
    return r;
}
void tsa_packet_ring_destroy(tsa_packet_ring_t* r) {
    if (!r) return;
    if (r->buffer) free(r->buffer);
    if (r->timestamps) free(r->timestamps);
    free(r);
}
int tsa_packet_ring_push(tsa_packet_ring_t* r, const uint8_t* p, uint64_t n) {
    if (!r || !p) return -1;
    uint64_t h = atomic_load_explicit(&r->head, memory_order_relaxed),
             t = atomic_load_explicit(&r->tail, memory_order_acquire);
    if (h - t >= r->sz) return -1;
    size_t idx = h % r->sz;
    memcpy(r->buffer + idx * 188, p, 188);
    r->timestamps[idx] = n;
    atomic_store_explicit(&r->head, h + 1, memory_order_release);
    return 0;
}
int tsa_packet_ring_pop(tsa_packet_ring_t* r, uint8_t* p, uint64_t* n) {
    if (!r || !p) return -1;
    uint64_t t = atomic_load_explicit(&r->tail, memory_order_relaxed),
             h = atomic_load_explicit(&r->head, memory_order_acquire);
    if (h == t) return -1;
    size_t idx = t % r->sz;
    memcpy(p, r->buffer + idx * 188, 188);
    if (n) *n = r->timestamps[idx];
    atomic_store_explicit(&r->tail, t + 1, memory_order_release);
    return 0;
}
bool tsa_forensic_trigger(tsa_handle_t* h, int r) {
    if (!h) return false;
    uint64_t c = h->live->sync_loss.count + h->live->pat_error.count + h->live->cc_error.count +
                 h->live->pmt_error.count + h->live->pid_error.count + h->live->crc_error.count;
    if (h->last_trigger_reason == -1 || h->last_trigger_reason != r) {
        h->last_trigger_reason = r;
        h->last_forensic_alarm_count = c;
        return true;
    }
    if (c > h->last_forensic_alarm_count + 5) {
        h->last_forensic_alarm_count = c;
        return true;
    }
    return false;
}
struct tsa_forensic_writer {
    tsa_packet_ring_t* ring;
    FILE* fp;
    pthread_t thread;
    _Atomic bool running;
    char filename[256];
};
static void* forensic_writer_thread(void* arg) {
    tsa_forensic_writer_t* w = (tsa_forensic_writer_t*)arg;
    uint8_t pkt[188];
    uint64_t ts;
    while (atomic_load(&w->running)) {
        if (tsa_packet_ring_pop(w->ring, pkt, &ts) == 0) {
            if (fwrite(pkt, 1, 188, w->fp) != 188) break;
        } else {
            usleep(10000);
        }
    }
    return NULL;
}
tsa_forensic_writer_t* tsa_forensic_writer_create(tsa_packet_ring_t* r, const char* f) {
    if (!r || !f) return NULL;
    tsa_forensic_writer_t* w = calloc(1, sizeof(struct tsa_forensic_writer));
    if (!w) return NULL;
    w->ring = r;
    strncpy(w->filename, f, 255);
    w->fp = fopen(f, "wb");
    if (!w->fp) {
        free(w);
        return NULL;
    }
    return w;
}
void tsa_forensic_writer_destroy(tsa_forensic_writer_t* w) {
    if (!w) return;
    atomic_store(&w->running, false);
    if (w->thread) pthread_join(w->thread, NULL);
    if (w->fp) fclose(w->fp);
    free(w);
}
void tsa_forensic_writer_start(tsa_forensic_writer_t* w) {
    if (!w || atomic_load(&w->running)) return;
    atomic_store(&w->running, true);
    pthread_create(&w->thread, NULL, forensic_writer_thread, w);
}
void tsa_forensic_writer_stop(tsa_forensic_writer_t* w) {
    if (!w || !atomic_load(&w->running)) return;
    atomic_store(&w->running, false);
    pthread_join(w->thread, NULL);
    w->thread = 0;
}
int tsa_forensic_writer_write_all(tsa_forensic_writer_t* w) {
    if (!w || !w->fp || !w->ring) return -1;
    uint8_t p[188];
    uint64_t n;
    int c = 0;
    while (tsa_packet_ring_pop(w->ring, p, &n) == 0) {
        if (fwrite(p, 1, 188, w->fp) != 188) break;
        c++;
    }
    fflush(w->fp);
    return c;
}

double calculate_shannon_entropy(const uint32_t* counts, int len) {
    if (!counts || len <= 0) return 0;
    uint64_t total = 0;
    for (int i = 0; i < len; i++) total += counts[i];
    if (total == 0) return 0;
    double e = 0;
    for (int i = 0; i < len; i++) {
        if (counts[i] > 0) {
            double p = (double)counts[i] / total;
            e -= p * log2(p);
        }
    }
    return e;
}

double calculate_pcr_jitter(uint64_t pcr, uint64_t now, double* drift) {
    (void)pcr;
    (void)now;
    if (drift) *drift = 0;
    return 0;
}



/*
 * Industrial-Grade-style Stream Sync Lock Implementation
 */
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
                if (h->sync_state == TS_SYNC_LOCKED) {
                    tsa_process_packet(h, h->sync_buffer, now_ns);
                } else if (++h->sync_confirm_count >= 5) {
                    h->sync_state = TS_SYNC_LOCKED;
                    h->signal_lock = true;
                }
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
                processed++;
                h->sync_state = TS_SYNC_HUNTING;
                h->signal_lock = false;
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
