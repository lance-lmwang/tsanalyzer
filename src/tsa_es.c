#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tsa_internal.h"

static void parse_h264_sps(tsa_handle_t* h, uint16_t pid, const uint8_t* buf, int size) {
    if (size < 10) return;
    bit_reader_t r = {buf, size, 0};
    read_bits(&r, 8);
    h->pid_profile[pid] = read_bits(&r, 8);
    read_bits(&r, 16);
    read_ue(&r);
    if (h->pid_profile[pid] >= 100) {
        if (read_ue(&r) == 3) read_bits(&r, 1);
        read_ue(&r);
        read_ue(&r);
        read_bits(&r, 1);
        if (read_bits(&r, 1)) {
            for (int i = 0; i < 8; i++) {
                if (read_bits(&r, 1)) {
                    int sz = (i < 6) ? 16 : 64;
                    int last = 8, next = 8;
                    for (int j = 0; j < sz; j++) {
                        if (next != 0) next = (last + read_ue(&r)) % 256;
                        last = (next == 0) ? last : next;
                    }
                }
            }
        }
    }
    h->pid_log2_max_frame_num[pid] = read_ue(&r) + 4;
    uint32_t poc_t = read_ue(&r);
    if (poc_t == 0)
        read_ue(&r);
    else if (poc_t == 1) {
        read_bits(&r, 1);
        read_ue(&r);
        read_ue(&r);
        uint32_t num = read_ue(&r);
        for (uint32_t i = 0; i < num; i++) read_ue(&r);
    }
    read_ue(&r);
    read_bits(&r, 1);
    uint32_t pw = read_ue(&r);
    uint32_t ph = read_ue(&r);
    uint32_t mb = read_bits(&r, 1);
    if (!mb) read_bits(&r, 1);
    read_bits(&r, 1);
    uint32_t w = (pw + 1) * 16;
    uint32_t he = (ph + 1) * 16 * (2 - mb);
    if (read_bits(&r, 1)) {
        uint32_t cl = read_ue(&r), cr = read_ue(&r), ct = read_ue(&r), cb = read_ue(&r);
        w -= (cl + cr) * 2;
        he -= (ct + cb) * 2;
    }
    h->pid_width[pid] = w;
    h->pid_height[pid] = he;
}

static void parse_h265_sps(tsa_handle_t* h, uint16_t pid, const uint8_t* buf, int size) {
    if (size < 20) return;
    h265_reader_t r = {buf, size, 0, 0};
    read_bits_h265(&r, 16);
    read_bits_h265(&r, 4);
    uint32_t layers = read_bits_h265(&r, 3) + 1;
    read_bits_h265(&r, 1);
    read_bits_h265(&r, 2);
    read_bits_h265(&r, 1);
    h->pid_profile[pid] = read_bits_h265(&r, 5);
    read_bits_h265(&r, 32);
    read_bits_h265(&r, 1);
    read_bits_h265(&r, 1);
    read_bits_h265(&r, 1);
    read_bits_h265(&r, 1);
    read_bits_h265(&r, 44);
    read_bits_h265(&r, 8);
    uint8_t sp[8], sl[8];
    for (uint32_t i = 0; i < layers - 1; i++) {
        sp[i] = read_bits_h265(&r, 1);
        sl[i] = read_bits_h265(&r, 1);
    }
    if (layers > 1) {
        for (uint32_t i = layers - 1; i < 8; i++) read_bits_h265(&r, 2);
    }
    for (uint32_t i = 0; i < layers - 1; i++) {
        if (sp[i]) {
            read_bits_h265(&r, 2);
            read_bits_h265(&r, 1);
            read_bits_h265(&r, 5);
            read_bits_h265(&r, 32);
            read_bits_h265(&r, 48);
        }
        if (sl[i]) read_bits_h265(&r, 8);
    }
    read_ue_h265(&r);
    uint32_t chroma = read_ue_h265(&r);
    if (chroma == 3) read_bits_h265(&r, 1);
    h->pid_width[pid] = read_ue_h265(&r);
    h->pid_height[pid] = read_ue_h265(&r);
}

static void parse_aac_adts(tsa_handle_t* h, uint16_t pid, const uint8_t* buf, int size) {
    if (size < 7 || buf[0] != 0xFF || (buf[1] & 0xF0) != 0xF0) return;
    uint8_t prof = (buf[2] >> 6) + 1, f_idx = (buf[2] >> 2) & 0x0F, ch = ((buf[2] & 0x01) << 2) | (buf[3] >> 6);
    const uint32_t rates[] = {96000, 88200, 64000, 48000, 44100, 32000, 24000, 22050,
                              16000, 12000, 11025, 8000,  7350,  0,     0,     0};
    h->pid_profile[pid] = prof;
    if (f_idx < 13) h->pid_audio_sample_rate[pid] = rates[f_idx];
    h->pid_audio_channels[pid] = ch;
}

static void parse_mpeg_audio(tsa_handle_t* h, uint16_t pid, const uint8_t* buf, int size) {
    if (size < 4 || buf[0] != 0xFF || (buf[1] & 0xE0) != 0xE0) return;
    uint8_t id = (buf[1] >> 3) & 0x01, lay = (buf[1] >> 1) & 0x03, f_idx = (buf[2] >> 2) & 0x03,
            mod = (buf[3] >> 6) & 0x03;
    uint32_t rates[2][3] = {{22050, 24000, 16000}, {44100, 48000, 32000}};
    if (f_idx < 3) h->pid_audio_sample_rate[pid] = rates[id][f_idx];
    h->pid_audio_channels[pid] = (mod == 3) ? 1 : 2;
    h->pid_profile[pid] = 4 - lay;
}

static void tsa_handle_video_frame(tsa_handle_t* h, uint16_t pid, int nal_type, const uint8_t* payload, int len,
                                   bool is_h265) {
    bool is_idr = false;
    int slice_type = -1;

    if (is_h265) {
        is_idr = (nal_type == 19 || nal_type == 20);
    } else {
        is_idr = (nal_type == 5);
        if (nal_type == 1 && len > 1) {  // non-IDR slice
            bit_reader_t r = {payload, len, 0};
            read_ue(&r);  // first_mb_in_slice
            slice_type = read_ue(&r);
            if (slice_type > 4) slice_type -= 5;
        }
    }

    if (is_idr || slice_type == 2 || slice_type == 7) {  // I slice
        h->pid_i_frames[pid]++;
        h->pid_gop_n[pid]++;
        if (is_idr) {
            h->pid_last_gop_n[pid] = h->pid_gop_n[pid];
            if (h->pid_gop_min[pid] == 0 || h->pid_last_gop_n[pid] < h->pid_gop_min[pid])
                h->pid_gop_min[pid] = h->pid_last_gop_n[pid];
            if (h->pid_last_gop_n[pid] > h->pid_gop_max[pid]) h->pid_gop_max[pid] = h->pid_last_gop_n[pid];
            h->pid_gop_n[pid] = 0;
            if (h->pid_last_idr_ns[pid] > 0) {
                h->pid_gop_ms[pid] = (uint32_t)((h->stc_ns - h->pid_last_idr_ns[pid]) / 1000000ULL);
            }
            h->pid_last_idr_ns[pid] = h->stc_ns;
        }
    } else if (slice_type == 0 || slice_type == 5) {  // P slice
        h->pid_p_frames[pid]++;
        h->pid_gop_n[pid]++;
    } else if (slice_type == 1 || slice_type == 6) {  // B slice
        h->pid_b_frames[pid]++;
        h->pid_gop_n[pid]++;
    }
}

static void parse_ac3_audio(tsa_handle_t* h, uint16_t pid, const uint8_t* buf, int size) {
    if (size < 6 || buf[0] != 0x0B || buf[1] != 0x77) return;
    uint8_t fsc = (buf[4] >> 6) & 0x03, acm = buf[6] >> 5;
    uint32_t r[] = {48000, 44100, 32000, 0};
    uint8_t c[] = {2, 1, 2, 3, 3, 4, 4, 5};
    if (fsc < 3) h->pid_audio_sample_rate[pid] = r[fsc];
    if (acm < 8) h->pid_audio_channels[pid] = c[acm];
}

void tsa_handle_es_payload(tsa_handle_t* h, uint16_t pid, const uint8_t* pay, int len, uint64_t now) {
    (void)now;
    const char* st = tsa_get_pid_type_name(h, pid);
    if (strcmp(st, "AAC") == 0 || strcmp(st, "ADTS-AAC") == 0 || strcmp(st, "AAC-LATM") == 0) {
        for (int i = 0; i <= len - 7; i++) {
            if (pay[i] == 0xFF && (pay[i + 1] & 0xF0) == 0xF0) {
                parse_aac_adts(h, pid, pay + i, len - i);
                break;
            }
        }
        return;
    } else if (strcmp(st, "MPEG1-A") == 0 || strcmp(st, "MPEG2-A") == 0) {
        for (int i = 0; i <= len - 4; i++) {
            if (pay[i] == 0xFF && (pay[i + 1] & 0xE0) == 0xE0) {
                parse_mpeg_audio(h, pid, pay + i, len - i);
                break;
            }
        }
        return;
    } else if (strcmp(st, "AC3") == 0) {
        for (int i = 0; i <= len - 6; i++) {
            if (pay[i] == 0x0B && pay[i + 1] == 0x77) {
                parse_ac3_audio(h, pid, pay + i, len - i);
                break;
            }
        }
        return;
    }
    bool is_h264 = (strcmp(st, "H.264") == 0);
    bool is_h265 = (strcmp(st, "HEVC") == 0);
    if (!is_h264 && !is_h265 && strcmp(st, "ES") != 0) return;

    const uint8_t* p = pay;
    const uint8_t* end = pay + len - 4;
    while (p <= end) {
        p = memchr(p, 0x00, end - p + 1);
        if (!p) break;
        if (p[1] == 0x00 && p[2] == 0x01) {
            uint8_t nt;
            const uint8_t* d = p + 3;
            int l = len - (int)(d - pay);
            if (l <= 0) {
                p++;
                continue;
            }

            bool is_idr = false, is_sps = false;
            if (is_h264) {
                nt = d[0] & 0x1F;
                is_sps = (nt == 7);
                is_idr = (nt == 5);
            } else if (is_h265) {
                nt = (d[0] & 0x7E) >> 1;
                is_sps = (nt == 33);
                is_idr = (nt == 19 || nt == 20);
            } else {
                nt = d[0] & 0x1F;
                is_sps = (nt == 7);
                is_idr = (nt == 5);
            }

            if (is_sps) {
                if (is_h264)
                    parse_h264_sps(h, pid, d, l);
                else if (is_h265)
                    parse_h265_sps(h, pid, d, l);
            } else if (is_idr || (is_h264 && nt == 1) || (is_h265 && (nt >= 0 && nt <= 9))) {
                tsa_handle_video_frame(h, pid, nt, d, l, is_h265);
            }
            p += 3;
        } else {
            p++;
        }
    }
}
