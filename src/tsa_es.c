#include <stdlib.h>
#include <string.h>

#include "tsa_internal.h"

static void parse_h264_sps(tsa_handle_t* h, uint16_t pid, const uint8_t* buf, int size) {
    if (size < 10) return;
    bit_reader_t r = {buf, size, 0};
    read_bits(&r, 8);
    h->pid_profile[pid] = read_bits(&r, 8);
    read_bits(&r, 16);
    read_ue(&r);  // seq_parameter_set_id
    if (h->pid_profile[pid] == 100 || h->pid_profile[pid] == 110 || h->pid_profile[pid] == 122 ||
        h->pid_profile[pid] == 244 || h->pid_profile[pid] == 44 || h->pid_profile[pid] == 83 ||
        h->pid_profile[pid] == 86 || h->pid_profile[pid] == 118 || h->pid_profile[pid] == 128 ||
        h->pid_profile[pid] == 138 || h->pid_profile[pid] == 139 || h->pid_profile[pid] == 134) {
        if (read_ue(&r) == 3) read_bits(&r, 1);  // chroma_format_idc == 3
        read_ue(&r);                             // bit_depth_luma_minus8
        read_ue(&r);                             // bit_depth_chroma_minus8
        read_bits(&r, 1);                        // qpprime_y_zero_transform_bypass_flag
        if (read_bits(&r, 1)) {                  // seq_scaling_matrix_present_flag
            for (int i = 0; i < 8; i++) {
                if (read_bits(&r, 1)) {  // seq_scaling_list_present_flag
                    // Skip scaling list
                }
            }
        }
    }
    h->pid_log2_max_frame_num[pid] = read_ue(&r) + 4;
    uint32_t pic_order_cnt_type = read_ue(&r);
    if (pic_order_cnt_type == 0) {
        read_ue(&r);  // log2_max_pic_order_cnt_lsb_minus4
    } else if (pic_order_cnt_type == 1) {
        // Skip pic order cnt type 1
    }
    read_ue(&r);                                     // num_ref_frames
    read_bits(&r, 1);                                // gaps_in_frame_num_value_allowed_flag
    h->pid_width[pid] = (read_ue(&r) + 1) * 16;      // pic_width_in_mbs_minus1
    h->pid_height[pid] = (read_ue(&r) + 1) * 16;     // pic_height_in_map_units_minus1
    if (!read_bits(&r, 1)) h->pid_height[pid] *= 2;  // frame_mbs_only_flag
}

static void parse_h265_sps(tsa_handle_t* h, uint16_t pid, const uint8_t* buf, int size) {
    if (size < 20) return;
    bit_reader_t r = {buf, size, 0};
    read_bits(&r, 16);  // nal_unit_header
    read_bits(&r, 4);   // sps_video_parameter_set_id
    read_bits(&r, 3);   // sps_max_sub_layers_minus1
    read_bits(&r, 1);   // sps_temporal_id_nesting_flag
    // profile_tier_level
    read_bits(&r, 2);   // general_profile_space
    read_bits(&r, 1);   // general_tier_flag
    h->pid_profile[pid] = read_bits(&r, 5);
    read_bits(&r, 32);  // general_profile_compatibility_flag
    read_bits(&r, 48);  // constraints
    read_bits(&r, 8);   // general_level_idc
    // ... skipping rest of PTL
    read_ue(&r);  // sps_seq_parameter_set_id
    read_ue(&r);  // chroma_format_idc
    h->pid_width[pid] = (uint16_t)read_ue(&r);
    h->pid_height[pid] = (uint16_t)read_ue(&r);
}

void tsa_handle_video_frame(tsa_handle_t* h, uint16_t pid, uint8_t nalu_type, const uint8_t* buf, int size,
                            bool is_h265) {
    (void)buf;
    (void)size;
    bool is_idr = false;
    if (is_h265) {
        is_idr = (nalu_type >= 16 && nalu_type <= 21);
    } else {
        is_idr = (nalu_type == 5);
    }

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
        h->pid_p_frames[pid]++;
    }
}

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

            uint8_t nt = 0;
            bool is_idr = false, is_sps = false, is_sei = false;
            if (is_h264) {
                nt = d[0] & 0x1F;
                is_sps = (nt == 7);
                is_idr = (nt == 5);
                is_sei = (nt == 6);
            } else if (is_h265) {
                nt = (d[0] & 0x7E) >> 1;
                is_sps = (nt == 33);
                is_idr = (nt >= 16 && nt <= 21);
                is_sei = (nt == 39 || nt == 40);
            }

            if (is_sps) {
                if (is_h264)
                    parse_h264_sps(h, pid, d, l);
                else if (is_h265)
                    parse_h265_sps(h, pid, d, l);
            } else if (is_idr || (is_h264 && nt == 1) || (is_h265 && (nt >= 0 && nt <= 9))) {
                tsa_handle_video_frame(h, pid, nt, d, l, is_h265);
            } else if (is_sei) {
                // Quick scan for CEA-708 magic 'GA94' inside SEI
                for (int i = 0; i < l - 4 && i < 128; i++) {
                    if (d[i] == 'G' && d[i + 1] == 'A' && d[i + 2] == '9' && d[i + 3] == '4') {
                        h->pid_has_cea708[pid] = true;
                        break;
                    }
                }
            }
            p += 3;
        } else {
            p++;
        }
    }
}
