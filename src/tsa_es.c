#define _GNU_SOURCE
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "nalu_sniffer.h"
#include "tsa_internal.h"
#include "tsa_log.h"
#include "tsa_units.h"

#define TAG "ES_ANALYZER"

/* LTN UUID for High Precision Forensic SEI (Aligned with test expectations) */
static const uint8_t ltn_uuid_sei_timestamp[16] = {0x59, 0x96, 0xFF, 0x28, 0x17, 0xCA, 0x41, 0x96,
                                                   0x8D, 0xE3, 0xE5, 0x3F, 0xE2, 0xF9, 0x92, 0xAE};

static void tsa_handle_sei(tsa_handle_t* h, tsa_es_track_t* es, const uint8_t* payload, int len) {
    (void)h;
    if (len < 64) return;
    const uint8_t* p = memmem(payload, len, ltn_uuid_sei_timestamp, 16);
    if (!p) return;

    if (len - (p - payload) < 64) return;

    /* Fields 4/5: Begin time (Encoder Entry), Fields 6/7: End time (Encoder Exit) */
    p += 16;
    uint32_t v[8];
    for (int i = 0; i < 8; i++) {
        v[i] = (p[0] << 24) | (p[1] << 16) | (p[3] << 8) | p[4];
        p += 6;
    }

    uint64_t begin_ms = (uint64_t)v[4] * 1000ULL + (uint64_t)v[5] / 1000ULL;
    uint64_t end_ms = (uint64_t)v[6] * 1000ULL + (uint64_t)v[7] / 1000ULL;

    if (end_ms >= begin_ms) {
        es->video.encoder_latency_ms = end_ms - begin_ms;
    }

    uint64_t now_ms = ts_now_utc_ms();
    if (now_ms >= end_ms) {
        es->video.network_latency_ms = now_ms - end_ms;
    }
    es->video.last_sei_utc_ms = end_ms;
}

/* Helper: Get a pointer to data at absolute offset within a zero-copy PES accumulator */
static const uint8_t* tsa_pes_get_ptr(tsa_es_track_t* es, uint32_t offset, uint32_t* available) {
    uint32_t current = 0;
    for (uint32_t i = 0; i < es->pes.ref_count; i++) {
        uint32_t len = es->pes.payload_lens[i];
        if (offset >= current && offset < current + len) {
            uint32_t local_off = offset - current;
            *available = len - local_off;
            return (const uint8_t*)es->pes.refs[i]->data + es->pes.payload_offsets[i] + local_off;
        }
        current += len;
    }
    return NULL;
}

/* Helper: Copy data from zero-copy PES accumulator into a contiguous buffer */
static uint32_t tsa_pes_copy_data(tsa_es_track_t* es, uint32_t offset, uint8_t* dest, uint32_t len) {
    uint32_t copied = 0;
    while (copied < len) {
        uint32_t avail = 0;
        const uint8_t* p = tsa_pes_get_ptr(es, offset + copied, &avail);
        if (!p) break;
        uint32_t to_copy = (len - copied < avail) ? (len - copied) : avail;
        memcpy(dest + copied, p, to_copy);
        copied += to_copy;
    }
    return copied;
}

/* Fast check for start code cross-packet */
static bool tsa_pes_check_start_code(tsa_es_track_t* es, uint32_t offset) {
    uint8_t buf[3];
    if (tsa_pes_copy_data(es, offset, buf, 3) < 3) return false;
    return buf[0] == 0x00 && buf[1] == 0x00 && buf[2] == 0x01;
}

void tsa_handle_es_payload(tsa_handle_t* h, uint16_t pid, const uint8_t* payload, int payload_len, uint64_t stc_ns) {
    (void)payload;
    (void)payload_len;
    (void)stc_ns;
    tsa_es_track_t* es = &h->es_tracks[pid];

    if (es->pes.total_length < 6) return;

    /* 1. Parse PES Header to find the real start of ES data */
    uint8_t header_buf[32];
    uint32_t header_copied = tsa_pes_copy_data(es, 0, header_buf, 32);
    tsa_pes_header_t ph;
    if (tsa_parse_pes_header(header_buf, header_copied, &ph) != 0) {
        tsa_debug(TAG, "PID 0x%04x: Invalid PES header, skipping analysis", pid);
        return;
    }

    uint8_t st_id = es->stream_type;
    bool is_h264 = tsa_is_h264(st_id);
    bool is_hevc = tsa_is_hevc(st_id);
    if (!is_h264 && !is_hevc) return;

    bool frame_counted = false;
    uint32_t offset = ph.header_len; /* Start analysis AFTER the PES header */
    uint32_t total_len = es->pes.total_length;

    while (offset + 4 <= total_len) {
        uint32_t avail = 0;
        const uint8_t* p = tsa_pes_get_ptr(es, offset, &avail);
        if (!p) break;

        /* Optimize: Scan for 0x00 in current packet segment */
        const uint8_t* sc = memchr(p, 0x00, avail);
        if (!sc) {
            offset += avail;
            continue;
        }

        offset += (uint32_t)(sc - p);
        if (offset + 3 > total_len) break;

        if (tsa_pes_check_start_code(es, offset)) {
            uint32_t data_off = offset + 3;
            /* Optional: check for 0x01 if it was 00 00 00 01 */
            uint32_t dummy = 0;
            const uint8_t* p1 = tsa_pes_get_ptr(es, data_off, &dummy);
            if (p1 && *p1 == 0x01) data_off++;

            uint32_t data_avail = 0;
            const uint8_t* d = tsa_pes_get_ptr(es, data_off, &data_avail);

            if (d && data_avail > 0) {
                tsa_nalu_info_t info;
                tsa_nalu_sniff(d, data_avail, is_hevc, &info);

                if (info.nalu_type_abstract == NALU_TYPE_SPS) {
                    es->video.profile = info.profile;
                    es->video.level = info.level;
                    es->video.chroma_format = info.chroma_format;
                    if (info.bit_depth > 0) es->video.bit_depth = info.bit_depth;
                    if (info.width > 0) es->video.width = info.width;
                    if (info.height > 0) es->video.height = info.height;
                } else if (info.nalu_type_abstract == NALU_TYPE_SEI) {
                    tsa_handle_sei(h, es, d, data_avail);
                } else if (info.nalu_type_abstract == NALU_TYPE_IDR) {
                    es->video.is_closed_gop = info.is_closed_gop;
                    if (info.is_closed_gop)
                        es->video.closed_gops++;
                    else
                        es->video.open_gops++;

                    /* IDR takes absolute priority for marking GOP start */
                    if (es->pes.current_frame_type != 'I') {
                        if (es->pes.current_frame_type == 'P')
                            es->video.p_frames--;
                        else if (es->pes.current_frame_type == 'B')
                            es->video.b_frames--;

                        es->video.i_frames++;
                        es->pes.current_frame_type = 'I';

                        if (es->video.gop_str_idx > 0) {
                            es->video.gop_structure[es->video.gop_str_idx - 1] = 'I';
                        } else if (es->video.gop_str_idx < sizeof(es->video.gop_structure) - 1) {
                            es->video.gop_structure[es->video.gop_str_idx++] = 'I';
                            es->video.gop_structure[es->video.gop_str_idx] = '\0';
                        }
                    }
                    frame_counted = true;
                } else if (info.is_slice) {
                    bool is_idr = (info.slice_type == 2);
                    if (is_idr) {
                        /* Already handled above by nalu_type_abstract, but fallback for safety */
                        if (es->pes.current_frame_type != 'I') {
                            es->video.i_frames++;
                            es->pes.current_frame_type = 'I';
                        }
                    } else if (!frame_counted) {
                        char ftype = (info.slice_type == 1) ? 'B' : 'P';
                        if (ftype == 'B')
                            es->video.b_frames++;
                        else
                            es->video.p_frames++;
                        es->pes.current_frame_type = ftype;
                        if (es->video.gop_str_idx < sizeof(es->video.gop_structure) - 1) {
                            es->video.gop_structure[es->video.gop_str_idx++] = ftype;
                            es->video.gop_structure[es->video.gop_str_idx] = '\0';
                        }
                        frame_counted = true;
                    }
                }
            }
            offset += 3;
        } else {
            offset++;
        }
    }
}

void tsa_es_track_push_packet(tsa_handle_t* h, uint16_t pid, const uint8_t* pkt, const ts_decode_result_t* res) {
    tsa_es_track_t* es = &h->es_tracks[pid];
    if (!res->pusi && (!res->has_payload || res->payload_len == 0)) return;

    tsa_packet_t* p_obj = pkt ? (tsa_packet_t*)((uint8_t*)pkt - offsetof(tsa_packet_t, data)) : NULL;

    if (res->pusi) {
        /* 1. Finalize previous PES packet */
        if (es->pes.ref_count > 0) {
            tsa_handle_es_payload(h, pid, NULL, 0, h->stc_ns);

            /* Update frame size stats (Industrial Metrology) */
            if (es->pes.current_frame_type == 'I')
                es->video.i_frame_size_bytes += es->pes.total_length;
            else if (es->pes.current_frame_type == 'P')
                es->video.p_frame_size_bytes += es->pes.total_length;
            else if (es->pes.current_frame_type == 'B')
                es->video.b_frame_size_bytes += es->pes.total_length;

            /* Push to AU Queue for T-STD Drain Timing */
            if (es->pes.pending_dts_ns > 0) {
                uint8_t next_tail = (es->au_q.tail + 1) % TSA_AU_QUEUE_SIZE;
                if (next_tail != es->au_q.head) {
                    es->au_q.queue[es->au_q.tail].dts_ns = es->pes.pending_dts_ns;
                    es->au_q.queue[es->au_q.tail].size = es->pes.total_length * 8; /* bits */
                    es->au_q.tail = next_tail;
                }
            }

            /* Unref all packets in the finalized PES */
            for (uint32_t i = 0; i < es->pes.ref_count; i++) {
                if (es->pes.refs[i]) tsa_packet_unref(h->pkt_pool, es->pes.refs[i]);
            }
        }

        /* 2. Reset PES state for the new packet */
        es->pes.ref_count = 0;
        es->pes.total_length = 0;
        es->pes.current_frame_type = 0;

        if (res->has_pes_header) {
            /* Handle DTS/PTS logic (extrapolated or explicit) */
            uint64_t dts_ticks = res->has_dts ? res->dts : (res->has_pts ? res->pts : 0);
            if (dts_ticks == 0 && es->pes.last_dts_33 > 0) {
                /* Extrapolate: 90000 / FPS. Default to 25fps (3600 ticks) if unknown. */
                uint32_t increment = (es->video.exact_fps > 0) ? (uint32_t)(90000.0f / es->video.exact_fps) : 3600;
                dts_ticks = es->pes.last_dts_33 + increment;
            }

            if (dts_ticks > 0) {
                es->pes.pending_dts_ns = (dts_ticks * 1000000ULL) / 90;
                es->pes.last_dts_33 = dts_ticks;
            }

            /* Professional Jitter Analysis: PTS vs Arrival STC (ISO/IEC 13818-1) */
            if (res->has_pts && es->last_pts_val > 0) {
                uint64_t pts_delta = (res->pts > es->last_pts_val) ? (res->pts - es->last_pts_val) : 0;
                uint64_t vstc_delta_ticks =
                    (h->stc_ns > es->last_pts_vstc) ? ((h->stc_ns - es->last_pts_vstc) * 90 / 1000000) : 0;

                if (pts_delta > 0 && vstc_delta_ticks > 0) {
                    int64_t jitter = (int64_t)pts_delta - (int64_t)vstc_delta_ticks;
                    es->pts_jitter_q64 = INT_TO_Q64_64(jitter);
                }
            }

            if (res->has_pts) {
                es->last_pts_val = res->pts;
                es->last_pts_vstc = h->stc_ns;
            }

            es->pes.last_pts_33 = res->pts;
            es->pes.has_pts = res->has_pts;
            es->pes.has_dts = res->has_dts;
        }
    }

    /* 3. Append current packet to accumulator */
    if (p_obj && es->pes.ref_count < TSA_PES_MAX_REFS) {
        es->pes.refs[es->pes.ref_count] = p_obj;
        es->pes.payload_offsets[es->pes.ref_count] = (uint16_t)(pkt + 4 + res->af_len - (uint8_t*)p_obj->data);
        es->pes.payload_lens[es->pes.ref_count] = (uint8_t)res->payload_len;
        es->pes.ref_count++;
        es->pes.total_length += res->payload_len;
        tsa_packet_ref(p_obj);
    }
}
