#include "metrology/nalu_sniffer.h"
#include <stdlib.h>
#include <string.h>

#include "tsa_internal.h"







void tsa_handle_es_payload(tsa_handle_t* h, uint16_t pid, const uint8_t* pay, int len, uint64_t stc_ns) {
    (void)stc_ns;
    if (!h || !pay || len < 4) return;

    if (h->config.analysis.entropy) {
        uint32_t counts[256] = {0};
        for (int i = 0; i < len; i++) {
            counts[pay[i]]++;
        }
        double entropy = calculate_shannon_entropy(counts, 256);
        if (entropy < 1.5) {
            tsa_push_event(h, TSA_EVENT_ENTROPY_FREEZE, pid, (uint64_t)(entropy * 100));
        }
    }

    const char* st = tsa_get_pid_type_name(h, pid);
    if (strcmp(st, "H.264") != 0 && strcmp(st, "HEVC") != 0 && strcmp(st, "ES") != 0) {
        return;
    }

    bool is_h264 = (strcmp(st, "H.264") == 0);
    (void)is_h264;
    bool is_h265 = (strcmp(st, "HEVC") == 0);

    const uint8_t* p = pay;
    const uint8_t* end = pay + len - 4;
    while (p <= end) {
        p = memchr(p, 0x00, end - p + 1);
        if (!p) break;
        if (p[1] == 0x00 && p[2] == 0x01) {
            const uint8_t* d = p + 3;
            int l = len - (int)(d - pay);
            if (l <= 0) {
                p++;
                continue;
            }

            tsa_nalu_info_t info;
            tsa_nalu_sniff(d, l, is_h265, &info);

            if (info.nalu_type_abstract == NALU_TYPE_SPS) {
                h->pid_profile[pid] = info.profile;
                h->pid_level[pid] = info.level;
                h->pid_chroma_format[pid] = info.chroma_format;
                if (info.width > 0) h->pid_width[pid] = info.width;
                if (info.height > 0) h->pid_height[pid] = info.height;
            } else if (info.nalu_type_abstract == NALU_TYPE_SEI) {
                if (info.has_cea708) {
                    h->pid_has_cea708[pid] = true;
                }
            } else if (info.is_slice) {
                // Determine if it's an IDR (I-Frame) based on type
                bool is_idr = (info.nalu_type_abstract == NALU_TYPE_IDR) || (info.slice_type == 2);

                if (is_idr) {
                    uint64_t now = (uint64_t)ts_now_ns128();
                    if (h->pid_last_idr_ns[pid] > 0) {
                        h->pid_gop_ms[pid] = (uint32_t)((now - h->pid_last_idr_ns[pid]) / 1000000ULL);
                        h->pid_last_gop_n[pid] = h->pid_gop_n[pid];
                        if (h->pid_gop_ms[pid] < h->pid_gop_min[pid]) h->pid_gop_min[pid] = h->pid_gop_ms[pid];
                        if (h->pid_gop_ms[pid] > h->pid_gop_max[pid]) h->pid_gop_max[pid] = h->pid_gop_ms[pid];
                    }
                    h->pid_last_idr_ns[pid] = now;
                    h->pid_gop_n[pid] = 0;
                    h->pid_i_frames[pid]++;

                    // SCTE-35 Alignment Audit
                    if (h->scte35_target_pts[pid] != 0xFFFFFFFFFFFFFFFFULL) {
                        if (h->pid_last_pts_33[pid] != 0x1FFFFFFFFULL) {
                            uint64_t frame_pts = h->pid_last_pts_33[pid];
                            uint64_t scte_pts = h->scte35_target_pts[pid];
                            // PTS is 90kHz
                            int64_t diff_90k = (int64_t)frame_pts - (int64_t)scte_pts;

                            // Handle rollover delta (if diff is wildly huge)
                            if (diff_90k > ((int64_t)1 << 32)) diff_90k -= ((int64_t)1 << 33);
                            else if (diff_90k < -((int64_t)1 << 32)) diff_90k += ((int64_t)1 << 33);

                            h->scte35_alignment_error_ns[pid] = diff_90k * 1000000000LL / 90000LL;

                            // Consume the target
                            h->scte35_target_pts[pid] = 0xFFFFFFFFFFFFFFFFULL;
                        }
                    }
                } else {
                    h->pid_gop_n[pid]++;
                    if (info.slice_type == 1) {
                        h->pid_b_frames[pid]++;
                    } else {
                        h->pid_p_frames[pid]++;
                    }
                }
            }
            p += 3;
        } else {
            p++;
        }
    }
}
