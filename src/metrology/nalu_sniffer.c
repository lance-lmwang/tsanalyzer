#include "metrology/nalu_sniffer.h"
#include "tsa_bitstream.h"
#include <string.h>

static void sniff_h264_sps(const uint8_t* buf, int size, tsa_nalu_info_t* out) {
    if (size < 10) return;
    bit_reader_t r;
    br_init(&r, buf, size);

    br_read(&r, 8); // forbidden_zero_bit, nal_ref_idc, nal_unit_type

    out->profile = br_read(&r, 8);
    br_read(&r, 8); // constraint_set0_flag, etc.
    out->level = br_read(&r, 8);

    br_read_ue(&r); // seq_parameter_set_id

    if (out->profile == 100 || out->profile == 110 || out->profile == 122 ||
        out->profile == 244 || out->profile == 44 || out->profile == 83 ||
        out->profile == 86 || out->profile == 118 || out->profile == 128 ||
        out->profile == 138 || out->profile == 139 || out->profile == 134) {

        out->chroma_format = br_read_ue(&r);
        if (out->chroma_format == 3) br_read(&r, 1); // separate_colour_plane_flag

        br_read_ue(&r); // bit_depth_luma_minus8
        br_read_ue(&r); // bit_depth_chroma_minus8
        br_read(&r, 1); // qpprime_y_zero_transform_bypass_flag

        if (br_read(&r, 1)) { // seq_scaling_matrix_present_flag
            int limit = (out->chroma_format != 3) ? 8 : 12;
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
        out->chroma_format = 1; // 4:2:0 default for Baseline/Main
    }

    br_read_ue(&r); // log2_max_frame_num_minus4
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

    out->width = (pic_width_in_mbs_minus1 + 1) * 16;
    out->height = (pic_height_in_map_units_minus1 + 1) * 16;

    bool frame_mbs_only_flag = br_read(&r, 1);
    if (!frame_mbs_only_flag) {
        out->height *= 2;
        br_read(&r, 1); // mb_adaptive_frame_field_flag
    }

    br_read(&r, 1); // direct_8x8_inference_flag

    if (br_read(&r, 1)) { // frame_cropping_flag
        uint32_t frame_crop_left_offset = br_read_ue(&r);
        uint32_t frame_crop_right_offset = br_read_ue(&r);
        uint32_t frame_crop_top_offset = br_read_ue(&r);
        uint32_t frame_crop_bottom_offset = br_read_ue(&r);

        int crop_unit_x = 1, crop_unit_y = 1;
        if (out->chroma_format == 1) { // 4:2:0
            crop_unit_x = 2;
            crop_unit_y = frame_mbs_only_flag ? 2 : 4;
        } else if (out->chroma_format == 2) { // 4:2:2
            crop_unit_x = 2;
            crop_unit_y = frame_mbs_only_flag ? 1 : 2;
        }

        out->width -= (frame_crop_left_offset + frame_crop_right_offset) * crop_unit_x;
        out->height -= (frame_crop_top_offset + frame_crop_bottom_offset) * crop_unit_y;
    }
}

static void sniff_h265_sps(const uint8_t* buf, int size, tsa_nalu_info_t* out) {
    if (size < 16) return;
    bit_reader_t r;
    br_init(&r, buf, size);

    br_read(&r, 16); // nal_unit_header

    br_read(&r, 4); // sps_video_parameter_set_id
    uint32_t sps_max_sub_layers_minus1 = br_read(&r, 3);
    br_read(&r, 1); // sps_temporal_id_nesting_flag

    // profile_tier_level
    out->profile = br_read(&r, 2); // general_profile_space
    br_read(&r, 1); // general_tier_flag
    out->profile = br_read(&r, 5); // general_profile_idc
    br_read(&r, 32); // general_profile_compatibility_flag
    br_read(&r, 48); // general_constraint_indicator_flags
    out->level = br_read(&r, 8); // general_level_idc

    uint8_t sub_layer_profile_present_flag[8] = {0};
    uint8_t sub_layer_level_present_flag[8] = {0};
    for (uint32_t i = 0; i < sps_max_sub_layers_minus1; i++) {
        sub_layer_profile_present_flag[i] = br_read(&r, 1);
        sub_layer_level_present_flag[i] = br_read(&r, 1);
    }
    if (sps_max_sub_layers_minus1 > 0) {
        for (uint32_t i = sps_max_sub_layers_minus1; i < 8; i++) {
            br_read(&r, 2); // reserved_zero_2bits
        }
    }
    for (uint32_t i = 0; i < sps_max_sub_layers_minus1; i++) {
        if (sub_layer_profile_present_flag[i]) {
            br_read(&r, 2);
            br_read(&r, 1);
            br_read(&r, 5);
            br_read(&r, 32);
            br_read(&r, 48);
        }
        if (sub_layer_level_present_flag[i]) {
            br_read(&r, 8);
        }
    }

    br_read_ue(&r); // sps_seq_parameter_set_id
    out->chroma_format = br_read_ue(&r); // chroma_format_idc
    if (out->chroma_format == 3) {
        br_read(&r, 1); // separate_colour_plane_flag
    }

    out->width = br_read_ue(&r); // pic_width_in_luma_samples
    out->height = br_read_ue(&r); // pic_height_in_luma_samples

    if (br_read(&r, 1)) { // conformance_window_flag
        uint32_t conf_win_left_offset = br_read_ue(&r);
        uint32_t conf_win_right_offset = br_read_ue(&r);
        uint32_t conf_win_top_offset = br_read_ue(&r);
        uint32_t conf_win_bottom_offset = br_read_ue(&r);

        int sub_wc = (out->chroma_format == 1 || out->chroma_format == 2) ? 2 : 1;
        int sub_hc = (out->chroma_format == 1) ? 2 : 1;
        out->width -= (conf_win_left_offset + conf_win_right_offset) * sub_wc;
        out->height -= (conf_win_top_offset + conf_win_bottom_offset) * sub_hc;
    }
}

void tsa_nalu_sniff(const uint8_t* buf, int size, bool is_h265, tsa_nalu_info_t* out_info) {
    if (!buf || size < 2 || !out_info) return;

    memset(out_info, 0, sizeof(tsa_nalu_info_t));
    out_info->is_h265 = is_h265;
    out_info->slice_type = -1;

    if (is_h265) {
        out_info->nalu_type_raw = (buf[0] & 0x7E) >> 1;

        if (out_info->nalu_type_raw == 33) {
            out_info->nalu_type_abstract = NALU_TYPE_SPS;
            sniff_h265_sps(buf, size, out_info);
        } else if (out_info->nalu_type_raw == 34) {
            out_info->nalu_type_abstract = NALU_TYPE_PPS;
        } else if (out_info->nalu_type_raw >= 16 && out_info->nalu_type_raw <= 21) {
            out_info->nalu_type_abstract = NALU_TYPE_IDR;
            out_info->is_slice = true;
            out_info->slice_type = 2; // I-slice
        } else if (out_info->nalu_type_raw >= 0 && out_info->nalu_type_raw <= 9) {
            out_info->nalu_type_abstract = NALU_TYPE_NON_IDR;
            out_info->is_slice = true;
            // Sniff slice type from slice segment header
            if (size >= 3) {
                bit_reader_t r;
                br_init(&r, buf + 2, size - 2); // Skip 2 byte header
                bool first_slice_segment_in_pic_flag = br_read(&r, 1);
                if (first_slice_segment_in_pic_flag) {
                    if (out_info->nalu_type_raw >= 16 && out_info->nalu_type_raw <= 23) {
                        br_read(&r, 1); // no_output_of_prior_pics_flag
                    }
                    br_read_ue(&r); // slice_pic_parameter_set_id
                    // Cannot easily parse slice_type in HEVC without knowing dependent_slice_segment_flag etc
                    // But we know it's a non-IDR, so typically P or B. Defaulting to P for basic accounting.
                    out_info->slice_type = 0;
                }
            }
        } else if (out_info->nalu_type_raw == 39 || out_info->nalu_type_raw == 40) {
            out_info->nalu_type_abstract = NALU_TYPE_SEI;
            for (int i = 0; i < size - 4 && i < 128; i++) {
                if (buf[i] == 'G' && buf[i + 1] == 'A' && buf[i + 2] == '9' && buf[i + 3] == '4') {
                    out_info->has_cea708 = true;
                    break;
                }
            }
        }
    } else {
        out_info->nalu_type_raw = buf[0] & 0x1F;

        if (out_info->nalu_type_raw == 7) {
            out_info->nalu_type_abstract = NALU_TYPE_SPS;
            sniff_h264_sps(buf, size, out_info);
        } else if (out_info->nalu_type_raw == 8) {
            out_info->nalu_type_abstract = NALU_TYPE_PPS;
        } else if (out_info->nalu_type_raw == 5) {
            out_info->nalu_type_abstract = NALU_TYPE_IDR;
            out_info->is_slice = true;
            out_info->slice_type = 2; // I-slice
        } else if (out_info->nalu_type_raw == 1) {
            out_info->nalu_type_abstract = NALU_TYPE_NON_IDR;
            out_info->is_slice = true;
            if (size >= 2) {
                bit_reader_t r;
                br_init(&r, buf + 1, size - 1); // Skip 1 byte header
                br_read_ue(&r); // first_mb_in_slice
                uint32_t slice_type = br_read_ue(&r);
                if (slice_type > 4) slice_type -= 5;
                // 0 = P, 1 = B, 2 = I, 3 = SP, 4 = SI
                out_info->slice_type = slice_type;
            }
        } else if (out_info->nalu_type_raw == 6) {
            out_info->nalu_type_abstract = NALU_TYPE_SEI;
            for (int i = 0; i < size - 4 && i < 128; i++) {
                if (buf[i] == 'G' && buf[i + 1] == 'A' && buf[i + 2] == '9' && buf[i + 3] == '4') {
                    out_info->has_cea708 = true;
                    break;
                }
            }
        }
    }
}
