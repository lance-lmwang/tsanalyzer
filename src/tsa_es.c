#include "metrology/nalu_sniffer.h"
#include <stdlib.h>
#include <string.h>

#include "tsa_internal.h"







void tsa_handle_es_payload(tsa_handle_t* h, uint16_t pid, const uint8_t* pay, int len, uint64_t stc_ns) {
    (void)stc_ns;
    if (!h || !pay || len < 4) return;

    const char* st = tsa_get_pid_type_name(h, pid);
    if (strcmp(st, "H.264") != 0 && strcmp(st, "HEVC") != 0 && strcmp(st, "ES") != 0) {
        return;
    }

    bool is_h264 = (strcmp(st, "H.264") == 0);
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
