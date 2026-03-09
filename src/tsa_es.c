#include <stdlib.h>
#include <string.h>

#include "nalu_sniffer.h"
#include "tsa_internal.h"
#include "tsa_log.h"

#define TAG "ES_ANALYZER"

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

/* Fast check for start code cross-packet (extremely rare but possible) */
static bool tsa_pes_check_start_code(tsa_es_track_t* es, uint32_t offset) {
    uint32_t avail = 0;
    const uint8_t* p = tsa_pes_get_ptr(es, offset, &avail);
    if (!p) return false;

    if (avail >= 3) {
        return p[0] == 0x00 && p[1] == 0x00 && p[2] == 0x01;
    } else {
        /* Fallback for split start code across packets */
        uint8_t buf[3];
        for (uint32_t i = 0; i < 3; i++) {
            const uint8_t* pb = tsa_pes_get_ptr(es, offset + i, &avail);
            if (!pb) return false;
            buf[i] = *pb;
        }
        return buf[0] == 0x00 && buf[1] == 0x00 && buf[2] == 0x01;
    }
}

void tsa_handle_es_payload(tsa_handle_t* h, uint16_t pid, const uint8_t* legacy_pay, int len, uint64_t stc_ns) {
    (void)legacy_pay;  // Ignored in Zero-Copy mode
    (void)len;
    if (!h) return;

    tsa_es_track_t* es = &h->es_tracks[pid];
    if (es->pes.total_length < 4) return;

    uint8_t st_id = es->stream_type;
    bool is_h264 = tsa_is_h264(st_id);
    bool is_h265 = tsa_is_hevc(st_id);
    if (!is_h264 && !is_h265) return;

    bool frame_counted = false;
    uint32_t offset = 0;
    uint32_t total_len = es->pes.total_length;

    while (offset + 4 <= total_len) {
        uint32_t avail = 0;
        const uint8_t* p = tsa_pes_get_ptr(es, offset, &avail);
        if (!p) break;

        /* Standard scan within a TS packet payload */
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
                /* If NALU is too fragmented, we might need a small stack buffer for the header */
                tsa_nalu_sniff(d, data_avail, is_h265, &info);

                if (info.nalu_type_abstract == NALU_TYPE_SPS) {
                    es->video.profile = info.profile;
                    es->video.level = info.level;
                    es->video.chroma_format = info.chroma_format;
                    if (info.bit_depth > 0) es->video.bit_depth = info.bit_depth;
                    if (info.width > 0) es->video.width = info.width;
                    if (info.height > 0) es->video.height = info.height;
                } else if (info.nalu_type_abstract == NALU_TYPE_IDR) {
                    es->video.is_closed_gop = info.is_closed_gop;
                    if (info.is_closed_gop)
                        es->video.closed_gops++;
                    else
                        es->video.open_gops++;
                    if (!frame_counted) {
                        es->video.i_frames++;
                        frame_counted = true;
                    }
                } else if (info.is_slice) {
                    bool is_idr = (info.nalu_type_abstract == NALU_TYPE_IDR) || (info.slice_type == 2);
                    if (is_idr) {
                        if (es->video.last_idr_ns > 0) {
                            uint32_t inst_gop_ms = (uint32_t)((stc_ns - es->video.last_idr_ns) / 1000000ULL);
                            /* EMA Smoothing for GOP metrics (Phase 2.1) */
                            if (es->video.gop_ms == 0)
                                es->video.gop_ms = inst_gop_ms;
                            else
                                es->video.gop_ms = (uint32_t)(0.2f * inst_gop_ms + 0.8f * es->video.gop_ms);

                            es->video.last_gop_n = es->video.gop_n;
                            if (inst_gop_ms < es->video.gop_min) es->video.gop_min = inst_gop_ms;
                            if (inst_gop_ms > es->video.gop_max) es->video.gop_max = inst_gop_ms;
                        }
                        es->video.last_idr_ns = stc_ns;
                        es->video.gop_n = 0;
                        if (!frame_counted) {
                            es->video.i_frames++;
                            frame_counted = true;
                        }
                    } else {
                        es->video.gop_n++;
                        if (!frame_counted) {
                            if (info.slice_type == 1)
                                es->video.b_frames++;
                            else
                                es->video.p_frames++;
                            frame_counted = true;
                        }
                    }
                }
            }
            offset += 3;
        } else {
            offset++;
        }
    }
}
