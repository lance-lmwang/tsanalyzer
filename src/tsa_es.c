#include <stdlib.h>
#include <string.h>

#include "tsa_internal.h"

static void parse_h264_sps(tsa_handle_t* h, uint16_t pid, const uint8_t* buf, int size) {
    if (size < 10) return;
    bit_reader_t r;
    br_init(&r, buf, size);

    br_read(&r, 8); // forbidden_zero_bit, nal_ref_idc, nal_unit_type

    h->pid_profile[pid] = br_read(&r, 8);
    br_read(&r, 8); // constraint_set0_flag, etc.
    h->pid_level[pid] = br_read(&r, 8);

    br_read_ue(&r); // seq_parameter_set_id

    if (h->pid_profile[pid] == 100 || h->pid_profile[pid] == 110 || h->pid_profile[pid] == 122 ||
        h->pid_profile[pid] == 244 || h->pid_profile[pid] == 44 || h->pid_profile[pid] == 83 ||
        h->pid_profile[pid] == 86 || h->pid_profile[pid] == 118 || h->pid_profile[pid] == 128 ||
        h->pid_profile[pid] == 138 || h->pid_profile[pid] == 139 || h->pid_profile[pid] == 134) {

        h->pid_chroma_format[pid] = br_read_ue(&r);
        if (h->pid_chroma_format[pid] == 3) br_read(&r, 1); // separate_colour_plane_flag

        h->pid_bit_depth[pid] = br_read_ue(&r) + 8; // bit_depth_luma_minus8
        br_read_ue(&r); // bit_depth_chroma_minus8
        br_read(&r, 1); // qpprime_y_zero_transform_bypass_flag

        if (br_read(&r, 1)) { // seq_scaling_matrix_present_flag
            int limit = (h->pid_chroma_format[pid] != 3) ? 8 : 12;
            for (int i = 0; i < limit; i++) {
                if (br_read(&r, 1)) { // seq_scaling_list_present_flag
                    int size_of_scaling_list = (i < 6) ? 16 : 64;
                    int last_scale = 8, next_scale = 8;
                    for (int j = 0; j < size_of_scaling_list; j++) {
                        if (next_scale != 0) {
                            int delta_scale = br_read_se(&r);
                            next_scale = (last_scale + delta_scale + 256) % 256;
                        }
                        last_scale = (next_scale == 0) ? last_scale : next_scale;
                    }
                }
            }
        }
    } else {
        h->pid_chroma_format[pid] = 1; // 4:2:0 default for Baseline/Main
        h->pid_bit_depth[pid] = 8;
    }

    h->pid_log2_max_frame_num[pid] = br_read_ue(&r) + 4;
    uint32_t pic_order_cnt_type = br_read_ue(&r);

    if (pic_order_cnt_type == 0) {
        br_read_ue(&r); // log2_max_pic_order_cnt_lsb_minus4
    } else if (pic_order_cnt_type == 1) {
        br_read(&r, 1); // delta_pic_order_always_zero_flag
        br_read_se(&r); // offset_for_non_ref_pic
        br_read_se(&r); // offset_for_top_to_bottom_field
        uint32_t num_ref_frames_in_pic_order_cnt_cycle = br_read_ue(&r);
        for (uint32_t i = 0; i < num_ref_frames_in_pic_order_cnt_cycle; i++) {
            br_read_se(&r); // offset_for_ref_frame
        }
    }

    br_read_ue(&r); // max_num_ref_frames
    br_read(&r, 1); // gaps_in_frame_num_value_allowed_flag

    uint32_t pic_width_in_mbs_minus1 = br_read_ue(&r);
    uint32_t pic_height_in_map_units_minus1 = br_read_ue(&r);

    h->pid_width[pid] = (pic_width_in_mbs_minus1 + 1) * 16;
    h->pid_height[pid] = (pic_height_in_map_units_minus1 + 1) * 16;

    bool frame_mbs_only_flag = br_read(&r, 1);
    if (!frame_mbs_only_flag) {
        h->pid_height[pid] *= 2;
        br_read(&r, 1); // mb_adaptive_frame_field_flag
    }

    br_read(&r, 1); // direct_8x8_inference_flag

    if (br_read(&r, 1)) { // frame_cropping_flag
        uint32_t frame_crop_left_offset = br_read_ue(&r);
        uint32_t frame_crop_right_offset = br_read_ue(&r);
        uint32_t frame_crop_top_offset = br_read_ue(&r);
        uint32_t frame_crop_bottom_offset = br_read_ue(&r);

        int crop_unit_x = 1, crop_unit_y = 1;
        if (h->pid_chroma_format[pid] == 1) { // 4:2:0
            crop_unit_x = 2;
            crop_unit_y = frame_mbs_only_flag ? 2 : 4;
        } else if (h->pid_chroma_format[pid] == 2) { // 4:2:2
            crop_unit_x = 2;
            crop_unit_y = frame_mbs_only_flag ? 1 : 2;
        }

        h->pid_width[pid] -= (frame_crop_left_offset + frame_crop_right_offset) * crop_unit_x;
        h->pid_height[pid] -= (frame_crop_top_offset + frame_crop_bottom_offset) * crop_unit_y;
    }

    if (br_read(&r, 1)) { // vui_parameters_present_flag
        if (br_read(&r, 1)) { // aspect_ratio_info_present_flag
            if (br_read(&r, 8) == 255) { // Extended_SAR
                br_read(&r, 16); // sar_width
                br_read(&r, 16); // sar_height
            }
        }
        if (br_read(&r, 1)) br_read(&r, 1); // overscan_info_present_flag
        if (br_read(&r, 1)) { // video_signal_type_present_flag
            br_read(&r, 3); // video_format
            br_read(&r, 1); // video_full_range_flag
            if (br_read(&r, 1)) { // colour_description_present_flag
                br_read(&r, 8); // colour_primaries
                br_read(&r, 8); // transfer_characteristics
                br_read(&r, 8); // matrix_coefficients
            }
        }
        if (br_read(&r, 1)) { // chroma_loc_info_present_flag
            br_read_ue(&r); br_read_ue(&r);
        }
        if (br_read(&r, 1)) { // timing_info_present_flag
            uint32_t num_units_in_tick = br_read(&r, 32);
            uint32_t time_scale = br_read(&r, 32);
            bool fixed_frame_rate_flag = br_read(&r, 1);
            if (num_units_in_tick > 0) {
                h->pid_exact_fps[pid] = (float)time_scale / (float)(2 * num_units_in_tick);
            }
        }
    }
}

static void parse_h265_sps(tsa_handle_t* h, uint16_t pid, const uint8_t* buf, int size) {
    if (size < 20) return;
    bit_reader_t r;
    br_init(&r, buf, size);
    br_read(&r, 16);  // nal_unit_header
    br_read(&r, 4);   // sps_video_parameter_set_id
    br_read(&r, 3);   // sps_max_sub_layers_minus1
    br_read(&r, 1);   // sps_temporal_id_nesting_flag
    // profile_tier_level
    br_read(&r, 2);   // general_profile_space
    br_read(&r, 1);   // general_tier_flag
    h->pid_profile[pid] = br_read(&r, 5);
    br_read(&r, 32);  // general_profile_compatibility_flag
    br_read(&r, 48);  // constraints
    br_read(&r, 8);   // general_level_idc
    // ... skipping rest of PTL
    br_read_ue(&r);  // sps_seq_parameter_set_id
    br_read_ue(&r);  // chroma_format_idc
    h->pid_width[pid] = (uint16_t)br_read_ue(&r);
    h->pid_height[pid] = (uint16_t)br_read_ue(&r);
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
