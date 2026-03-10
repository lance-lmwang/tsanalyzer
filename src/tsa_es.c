#define _GNU_SOURCE
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

/* Fast check for start code cross-packet */
static bool tsa_pes_check_start_code(tsa_es_track_t* es, uint32_t offset) {
    uint32_t avail = 0;
    const uint8_t* p = tsa_pes_get_ptr(es, offset, &avail);
    if (!p) return false;

    if (avail >= 3) {
        return p[0] == 0x00 && p[1] == 0x00 && p[2] == 0x01;
    } else {
        uint8_t buf[3];
        for (uint32_t i = 0; i < 3; i++) {
            uint32_t dummy = 0;
            const uint8_t* pb = tsa_pes_get_ptr(es, offset + i, &dummy);
            if (!pb) return false;
            buf[i] = *pb;
        }
        return buf[0] == 0x00 && buf[1] == 0x00 && buf[2] == 0x01;
    }
}

void tsa_handle_es_payload(tsa_handle_t* h, uint16_t pid, const uint8_t* payload, int payload_len, uint64_t stc_ns) {
    (void)payload;
    (void)payload_len;
    (void)stc_ns;
    tsa_es_track_t* es = &h->es_tracks[pid];

    if (es->pes.total_length < 4) return;

    uint8_t st_id = es->stream_type;
    bool is_h264 = tsa_is_h264(st_id);
    bool is_hevc = tsa_is_hevc(st_id);
    if (!is_h264 && !is_hevc) return;

    bool frame_counted = false;
    uint32_t offset = 0;
    uint32_t total_len = es->pes.total_length;

    while (offset + 4 <= total_len) {
        uint32_t avail = 0;
        const uint8_t* p = tsa_pes_get_ptr(es, offset, &avail);
        if (!p) break;

        /* Skip the first start code if it's right at the beginning (likely PES Start Code) */
        if (offset == 0 && p[0] == 0x00 && p[1] == 0x00 && p[2] == 0x01) {
            offset += 3;
            continue;
        }

        const uint8_t* sc = memchr(p, 0x00, avail);
        if (!sc) {
            offset += avail;
            continue;
        }

        offset += (uint32_t)(sc - p);
        if (offset + 3 > total_len) break;

        if (tsa_pes_check_start_code(es, offset)) {
            uint32_t data_off = offset + 3;
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
