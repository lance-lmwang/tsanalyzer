#define _GNU_SOURCE
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "tsa_internal.h"

/* --- Forward Declarations --- */
static void tsa_reset_pid_stats(tsa_handle_t* h, uint16_t pid);

/* --- High-precision Time Utilities (128-bit) --- */
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

/* --- Low-level String Utilities --- */
int tsa_fast_itoa(char* buf, int64_t val) {
    if (val == 0) {
        buf[0] = '0';
        buf[1] = '\0';
        return 1;
    }
    int i = 0, sign = (val < 0);
    if (sign) val = -val;
    while (val > 0) {
        buf[i++] = (val % 10) + '0';
        val /= 10;
    }
    if (sign) buf[i++] = '-';
    for (int j = 0; j < i / 2; j++) {
        char t = buf[j];
        buf[j] = buf[i - 1 - j];
        buf[i - 1 - j] = t;
    }
    buf[i] = '\0';
    return i;
}

int tsa_fast_ftoa(char* buf, float val, int precision) {
    int off = 0;
    if (val < 0) {
        buf[off++] = '-';
        val = -val;
    }

    float rounding = 0.5f;
    for (int i = 0; i < precision; i++) rounding /= 10.0f;
    val += rounding;

    int64_t integral = (int64_t)val;
    off += tsa_fast_itoa(buf + off, integral);

    if (precision <= 0) return off;
    buf[off++] = '.';

    float frac = val - (float)integral;

    for (int i = 0; i < precision; i++) {
        frac *= 10;
        int digit = (int)frac;
        buf[off++] = digit + '0';
        frac -= digit;
    }
    buf[off] = '\0';
    return off;
}

void tsa_mbuf_init(tsa_metric_buffer_t* b, char* buf, size_t sz) {
    b->ptr = buf;
    b->size = sz;
    b->offset = 0;
    if (sz > 0) b->ptr[0] = '\0';
}

void tsa_mbuf_append_str(tsa_metric_buffer_t* b, const char* s) {
    size_t len = strlen(s);
    size_t remaining = (b->offset < b->size) ? (b->size - b->offset) : 0;
    size_t to_copy = (len < remaining) ? len : remaining;
    if (to_copy > 0) {
        memcpy(b->ptr + b->offset, s, to_copy);
        b->offset += to_copy;
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
        case 0x1b:
            return "H.264";
        case 0x24:
            return "HEVC";
        case 0x81:
            return "AC3";
        default:
            return "Unknown";
    }
}

/* --- H.264/H.265/AAC ES 深度解析 --- */
typedef struct {
    const uint8_t* buf;
    int size;
    int pos;
} bit_reader_t;

static uint32_t read_bits(bit_reader_t* r, int n) {
    uint32_t val = 0;
    for (int i = 0; i < n; i++) {
        if (r->pos / 8 >= r->size) break;
        val = (val << 1) | ((r->buf[r->pos / 8] >> (7 - (r->pos % 8))) & 1);
        r->pos++;
    }
    return val;
}

static uint32_t read_ue(bit_reader_t* r) {
    int count = 0;
    while (read_bits(r, 1) == 0 && count < 32) count++;
    if (count >= 32) return 0;
    return (1 << count) - 1 + read_bits(r, count);
}

typedef struct {
    const uint8_t* buf;
    int size;
    int pos;
    int zeros;
} h265_reader_t;

static uint32_t read_bits_h265(h265_reader_t* r, int n) {
    uint32_t val = 0;
    for (int i = 0; i < n; i++) {
        if (r->pos / 8 >= r->size) break;
        if ((r->pos % 8 == 0) && r->zeros >= 2 && r->buf[r->pos / 8] == 0x03) {
            r->pos += 8;
            r->zeros = 0;
        }
        if (r->pos / 8 >= r->size) break;
        uint8_t bit = (r->buf[r->pos / 8] >> (7 - (r->pos % 8))) & 1;
        val = (val << 1) | bit;
        if (bit == 0)
            r->zeros++;
        else
            r->zeros = 0;
        r->pos++;
    }
    return val;
}

static uint32_t read_ue_h265(h265_reader_t* r) {
    int count = 0;
    while (read_bits_h265(r, 1) == 0 && count < 32) count++;
    if (count >= 32) return 0;
    return (1 << count) - 1 + read_bits_h265(r, count);
}

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
    uint32_t poc_type = read_ue(&r);
    if (poc_type == 0)
        read_ue(&r);
    else if (poc_type == 1) {
        read_bits(&r, 1);
        read_ue(&r);
        read_ue(&r);
        uint32_t num_ref = read_ue(&r);
        for (uint32_t i = 0; i < num_ref; i++) read_ue(&r);
    }
    read_ue(&r);
    read_bits(&r, 1);
    uint32_t pic_w = read_ue(&r);
    uint32_t pic_h = read_ue(&r);
    uint32_t frame_mbs_only = read_bits(&r, 1);
    if (!frame_mbs_only) read_bits(&r, 1);
    read_bits(&r, 1);
    uint32_t width = (pic_w + 1) * 16;
    uint32_t height = (pic_h + 1) * 16 * (2 - frame_mbs_only);
    if (read_bits(&r, 1)) {
        uint32_t cl = read_ue(&r), cr = read_ue(&r), ct = read_ue(&r), cb = read_ue(&r);
        width -= (cl + cr) * 2;
        height -= (ct + cb) * 2;
    }
    h->pid_width[pid] = width;
    h->pid_height[pid] = height;
}

static void parse_h265_sps(tsa_handle_t* h, uint16_t pid, const uint8_t* buf, int size) {
    if (size < 20) return;
    h265_reader_t r = {buf, size, 0, 0};
    read_bits_h265(&r, 16);
    read_bits_h265(&r, 4);
    uint32_t max_sub_layers = read_bits_h265(&r, 3) + 1;
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
    for (uint32_t i = 0; i < max_sub_layers - 1; i++) {
        sp[i] = read_bits_h265(&r, 1);
        sl[i] = read_bits_h265(&r, 1);
    }
    if (max_sub_layers > 1)
        for (uint32_t i = max_sub_layers - 1; i < 8; i++) read_bits_h265(&r, 2);
    for (uint32_t i = 0; i < max_sub_layers - 1; i++) {
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
    if (size < 7) return;
    if (buf[0] != 0xFF || (buf[1] & 0xF0) != 0xF0) return;
    uint8_t profile = (buf[2] >> 6) + 1;
    uint8_t freq_idx = (buf[2] >> 2) & 0x0F;
    uint8_t chan_cfg = ((buf[2] & 0x01) << 2) | (buf[3] >> 6);
    const uint32_t rates[] = {96000, 88200, 64000, 48000, 44100, 32000, 24000, 22050,
                              16000, 12000, 11025, 8000,  7350,  0,     0,     0};
    h->pid_profile[pid] = profile;
    if (freq_idx < 13) h->pid_audio_sample_rate[pid] = rates[freq_idx];
    h->pid_audio_channels[pid] = chan_cfg;
}

static void parse_mpeg_audio(tsa_handle_t* h, uint16_t pid, const uint8_t* buf, int size) {
    if (size < 4 || buf[0] != 0xFF || (buf[1] & 0xE0) != 0xE0) return;
    uint8_t id = (buf[1] >> 3) & 0x01;
    uint8_t layer = (buf[1] >> 1) & 0x03;
    uint8_t freq_idx = (buf[2] >> 2) & 0x03;
    uint8_t mode = (buf[3] >> 6) & 0x03;
    uint32_t rates[2][3] = {{22050, 24000, 16000}, {44100, 48000, 32000}};
    if (freq_idx < 3) h->pid_audio_sample_rate[pid] = rates[id][freq_idx];
    h->pid_audio_channels[pid] = (mode == 3) ? 1 : 2;
    h->pid_profile[pid] = 4 - layer;
}

static void parse_ac3_audio(tsa_handle_t* h, uint16_t pid, const uint8_t* buf, int size) {
    if (size < 6 || buf[0] != 0x0B || buf[1] != 0x77) return;
    uint8_t fscod = (buf[4] >> 6) & 0x03;
    uint8_t acmod = buf[6] >> 5;
    uint32_t rates[] = {48000, 44100, 32000, 0};
    uint8_t channels[] = {2, 1, 2, 3, 3, 4, 4, 5};
    if (fscod < 3) h->pid_audio_sample_rate[pid] = rates[fscod];
    if (acmod < 8) h->pid_audio_channels[pid] = channels[acmod];
}

void tsa_handle_es_payload(tsa_handle_t* h, uint16_t pid, const uint8_t* payload, int len, uint64_t now_ns) {
    const char* stream_type = tsa_get_pid_type_name(h, pid);
    if (strcmp(stream_type, "AAC") == 0 || strcmp(stream_type, "ADTS-AAC") == 0 ||
        strcmp(stream_type, "AAC-LATM") == 0) {
        for (int i = 0; i <= len - 7; i++)
            if (payload[i] == 0xFF && (payload[i + 1] & 0xF0) == 0xF0) {
                parse_aac_adts(h, pid, payload + i, len - i);
                break;
            }
        return;
    } else if (strcmp(stream_type, "MPEG1-A") == 0 || strcmp(stream_type, "MPEG2-A") == 0) {
        for (int i = 0; i <= len - 4; i++)
            if (payload[i] == 0xFF && (payload[i + 1] & 0xE0) == 0xE0) {
                parse_mpeg_audio(h, pid, payload + i, len - i);
                break;
            }
        return;
    } else if (strcmp(stream_type, "AC3") == 0) {
        for (int i = 0; i <= len - 6; i++)
            if (payload[i] == 0x0B && payload[i + 1] == 0x77) {
                parse_ac3_audio(h, pid, payload + i, len - i);
                break;
            }
        return;
    }
    bool is_h264 = (strcmp(stream_type, "H.264") == 0), is_h265 = (strcmp(stream_type, "HEVC") == 0);
    if (!is_h264 && !is_h265 && strcmp(stream_type, "ES") != 0) return;
    for (int i = 0; i < len - 4; i++) {
        if (payload[i] == 0x00 && payload[i + 1] == 0x00 && payload[i + 2] == 0x01) {
            uint8_t nal_type;
            const uint8_t* nalu_data = payload + i + 3;
            int nalu_len = len - i - 3;
            bool is_idr = false, is_sps = false;
            if (is_h264) {
                nal_type = payload[i + 3] & 0x1F;
                is_sps = (nal_type == 7);
                is_idr = (nal_type == 5);
            } else if (is_h265) {
                nal_type = (payload[i + 3] & 0x7E) >> 1;
                is_sps = (nal_type == 33);
                is_idr = (nal_type == 19 || nal_type == 20);
            } else {
                nal_type = payload[i + 3] & 0x1F;
                is_sps = (nal_type == 7);
                is_idr = (nal_type == 5);
            }
            if (is_sps) {
                if (is_h264)
                    parse_h264_sps(h, pid, nalu_data, nalu_len);
                else if (is_h265)
                    parse_h265_sps(h, pid, nalu_data, nalu_len);
            } else if (is_idr || (is_h264 && nal_type == 1) || (is_h265 && nal_type <= 21)) {
                if (nalu_len < 3) continue;
                bool is_new_frame = false;
                if (is_h264) {
                    bit_reader_t r = {nalu_data, nalu_len, 8};
                    uint32_t first_mb = read_ue(&r);
                    read_ue(&r);
                    read_ue(&r);
                    int fn_bits = h->pid_log2_max_frame_num[pid];
                    if (fn_bits == 0) fn_bits = 4;
                    uint32_t frame_num = read_bits(&r, fn_bits);
                    if (!h->pid_frame_num_valid[pid]) {
                        is_new_frame = (first_mb == 0);
                        h->pid_frame_num_valid[pid] = true;
                    } else if (frame_num != h->pid_last_frame_num[pid] || (first_mb == 0 && is_idr))
                        is_new_frame = true;
                    h->pid_last_frame_num[pid] = frame_num;
                } else if (is_h265) {
                    h265_reader_t r = {nalu_data, nalu_len, 16, 0};
                    is_new_frame = (read_bits_h265(&r, 1) == 1);
                }
                if (is_new_frame) {
                    if (is_idr) {
                        if (h->pid_last_idr_ns[pid] > 0) {
                            uint32_t cur_gop = h->pid_gop_n[pid];
                            h->pid_last_gop_n[pid] = cur_gop;
                            h->pid_gop_ms[pid] = (uint32_t)((now_ns - h->pid_last_idr_ns[pid]) / 1000000ULL);
                            if (cur_gop > 0) {
                                if (h->pid_gop_min[pid] == 0 || h->pid_gop_min[pid] == 0xFFFFFFFF ||
                                    cur_gop < h->pid_gop_min[pid])
                                    h->pid_gop_min[pid] = cur_gop;
                                if (cur_gop > h->pid_gop_max[pid]) h->pid_gop_max[pid] = cur_gop;
                            }
                        }
                        h->pid_last_idr_ns[pid] = now_ns;
                        h->pid_gop_n[pid] = 1;
                    } else
                        h->pid_gop_n[pid]++;
                }
            }
        }
    }
}

static void process_pat(tsa_handle_t* h, const uint8_t* pkt) {
    h->seen_pat = true;
    uint8_t af_len = (pkt[3] & 0x20) ? pkt[4] + 1 : 0;
    const uint8_t* payload = pkt + 4 + af_len;
    if (payload[0] == 0x00) payload++;
    int section_len = ((payload[1] & 0x0F) << 8) | payload[2];
    h->program_count = 0;
    for (int i = 8; i < section_len + 3 - 4; i += 4) {
        uint16_t prog_num = (payload[i] << 8) | payload[i + 1];
        uint16_t pmt_pid = ((payload[i + 2] & 0x1F) << 8) | payload[i + 3];
        if (prog_num != 0 && h->program_count < MAX_PROGRAMS) {
            h->pid_is_pmt[pmt_pid] = true;
            h->live.pid_is_referenced[pmt_pid] = true;
            h->programs[h->program_count].pmt_pid = pmt_pid;
            h->programs[h->program_count].stream_count = 0;
            h->program_count++;
        }
    }
}

static void process_pmt(tsa_handle_t* h, uint16_t pmt_pid, const uint8_t* pkt) {
    uint8_t af_len = (pkt[3] & 0x20) ? pkt[4] + 1 : 0;
    const uint8_t* payload = pkt + 4 + af_len;
    if (payload[0] == 0x00) payload++;
    int section_len = ((payload[1] & 0x0F) << 8) | payload[2];
    uint16_t pcr_pid = ((payload[8] & 0x1F) << 8) | payload[9];
    int pi_len = ((payload[10] & 0x0F) << 8) | payload[11];
    ts_program_info_t* prog = NULL;
    for (uint32_t i = 0; i < h->program_count; i++)
        if (h->programs[i].pmt_pid == pmt_pid) {
            prog = &h->programs[i];
            break;
        }
    if (prog) {
        for (uint32_t i = 0; i < prog->stream_count; i++) {
            uint16_t old_pid = prog->streams[i].pid;
            h->live.pid_is_referenced[old_pid] = false;
            tsa_reset_pid_stats(h, old_pid);
        }
        prog->pcr_pid = pcr_pid;
        if (!h->live.pid_is_referenced[pcr_pid]) {
            tsa_reset_pid_stats(h, pcr_pid);
        }
        h->live.pid_is_referenced[pcr_pid] = true;
        prog->stream_count = 0;
    }
    for (int i = 12 + pi_len; i < section_len + 3 - 4;) {
        uint8_t type = payload[i];
        uint16_t pid = ((payload[i + 1] & 0x1F) << 8) | payload[i + 2];
        int es_len = ((payload[i + 3] & 0x0F) << 8) | payload[i + 4];
        if (prog && prog->stream_count < MAX_STREAMS_PER_PROG) {
            prog->streams[prog->stream_count].pid = pid;
            prog->streams[prog->stream_count].stream_type = type;
            prog->stream_count++;
        }
        if (!h->live.pid_is_referenced[pid]) {
            tsa_reset_pid_stats(h, pid);
        }
        h->live.pid_is_referenced[pid] = true;
        i += 5 + es_len;
    }
}

tsa_handle_t* tsa_create(const tsa_config_t* cfg) {
    tsa_handle_t* h = calloc(1, sizeof(tsa_handle_t));
    if (h && cfg) h->config = *cfg;
    if (h && h->config.pcr_ema_alpha <= 0) h->config.pcr_ema_alpha = 0.05;
    if (h) {
        ts_pcr_window_init(&h->pcr_window, 100);
        h->pool_size = 1024 * 1024;
        if (posix_memalign(&h->pool_base, 64, h->pool_size) != 0) h->pool_base = malloc(h->pool_size);
        h->pool_offset = 0;
        for (int i = 0; i < TS_PID_MAX; i++) {
            h->pid_to_active_idx[i] = -1;
            h->pid_gop_min[i] = 0xFFFFFFFF;
        }
    }
    return h;
}

void tsa_destroy(tsa_handle_t* h) {
    if (h) {
        ts_pcr_window_destroy(&h->pcr_window);
        if (h->pool_base) free(h->pool_base);
        for (int i = 0; i < TS_PID_MAX; i++)
            if (h->pid_pes_buf[i]) free(h->pid_pes_buf[i]);
        free(h);
    }
}

static void tsa_reset_pid_stats(tsa_handle_t* h, uint16_t pid) {
    h->pid_seen[pid] = false;
    h->live.pid_packet_count[pid] = 0;
    h->live.pid_bitrate_bps[pid] = 0;
    h->live.pid_cc_errors[pid] = 0;
    h->pid_bitrate_min[pid] = 0;
    h->pid_bitrate_max[pid] = 0;
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
    h->pid_eb_fill_double[pid] = 0;
    h->pid_tb_fill_bytes[pid] = 0;
    h->pid_mb_fill_bytes[pid] = 0;
    h->live.pid_eb_fill_bytes[pid] = 0;
    h->live.pid_eb_fill_pct[pid] = 0;
}

void tsa_process_packet(tsa_handle_t* h, const uint8_t* pkt, uint64_t now_ns) {
    if (!h || !pkt) return;
    if (h->start_ns == 0) {
        h->start_ns = now_ns;
        h->last_snap_ns = now_ns;
        h->last_pcr_arrival_ns = now_ns;
        h->stc_ns = now_ns;
        h->last_pat_ns = now_ns;
        h->last_pmt_ns = now_ns;
    }

    if (pkt[0] != 0x47) {
        h->consecutive_sync_errors++;
        h->consecutive_good_syncs = 0;
        if (h->consecutive_sync_errors >= 5) {
            if (h->signal_lock) {
                h->live.sync_loss.count++;
                h->live.sync_loss.last_timestamp_ns = now_ns;
                h->signal_lock = false;
            }
        }
        h->live.sync_byte_error.count++;
        h->live.sync_byte_error.last_timestamp_ns = now_ns;
        return;
    } else {
        h->consecutive_good_syncs++;
        h->consecutive_sync_errors = 0;
        if (h->consecutive_good_syncs >= 2) {
            h->signal_lock = true;
        }
    }
    if (!h->stc_locked) h->stc_ns = now_ns;
    uint16_t pid = ((pkt[1] & 0x1F) << 8) | pkt[2];
    h->live.total_ts_packets++;
    int16_t pid_idx = h->pid_to_active_idx[pid];

    if (pid_idx == -1) {
        if (h->pid_tracker_count < 128) {
            h->pid_active_list[h->pid_tracker_count] = pid;
            h->pid_to_active_idx[pid] = h->pid_tracker_count;
            pid_idx = h->pid_tracker_count++;
        } else {
            int evict_idx = 0;
            for (int i = 0; i < 128; i++) {
                bool protected = false;
                for (int j = 0; j < 16; j++) {
                    if (h->config.protected_pids[j] == h->pid_active_list[i]) {
                        protected = true;
                        break;
                    }
                }
                if (!protected) {
                    evict_idx = i;
                    break;
                }
            }
            uint16_t evicted = h->pid_active_list[evict_idx];
            h->pid_to_active_idx[evicted] = -1;
            tsa_reset_pid_stats(h, evicted);
            for (int i = evict_idx; i < 127; i++) {
                h->pid_active_list[i] = h->pid_active_list[i + 1];
                h->pid_to_active_idx[h->pid_active_list[i]] = i;
            }
            h->pid_active_list[127] = pid;
            h->pid_to_active_idx[pid] = 127;
            pid_idx = 127;
        }
        h->pid_seen[pid] = true;
    }
    if (pid_idx != (int16_t)h->pid_tracker_count - 1) {
        uint16_t cur = h->pid_active_list[pid_idx];
        for (uint32_t i = (uint32_t)pid_idx; i < h->pid_tracker_count - 1; i++) {
            h->pid_active_list[i] = h->pid_active_list[i + 1];
            h->pid_to_active_idx[h->pid_active_list[i]] = i;
        }
        h->pid_active_list[h->pid_tracker_count - 1] = cur;
        h->pid_to_active_idx[cur] = h->pid_tracker_count - 1;
    }
    h->live.pid_packet_count[pid]++;

    // Phase 3: T-STD Leaky Bucket Simulator
    uint8_t pusi = (pkt[1] & 0x40);
    uint8_t af_len = (pkt[3] & 0x20) ? pkt[4] + 1 : 0;
    int payload_len = 188 - 4 - af_len;
    bool has_payload = (pkt[3] & 0x10) && payload_len > 0;

    if (h->live.pid_last_seen_ns[pid] > 0) {
        uint64_t dt = h->stc_ns - h->live.pid_last_seen_ns[pid];
        double leak_eb = (double)dt * 5.0 / 1000.0;   // 40 Mbps ES leak rate
        double leak_tb = (double)dt * 6.25 / 1000.0;  // 50 Mbps Transport rate

        h->pid_tb_fill_bytes[pid] -= leak_tb;
        if (h->pid_tb_fill_bytes[pid] < 0) h->pid_tb_fill_bytes[pid] = 0;

        h->pid_mb_fill_bytes[pid] -= leak_tb;
        if (h->pid_mb_fill_bytes[pid] < 0) h->pid_mb_fill_bytes[pid] = 0;

        if (h->live.pid_is_referenced[pid]) {
            h->pid_eb_fill_double[pid] -= leak_eb;
            if (h->pid_eb_fill_double[pid] < 0) h->pid_eb_fill_double[pid] = 0;
        }
    }

    h->pid_tb_fill_bytes[pid] += 188;
    if (has_payload) {
        h->pid_mb_fill_bytes[pid] += payload_len;
        if (h->live.pid_is_referenced[pid]) {
            h->pid_eb_fill_double[pid] += payload_len;
        }
    }
    if (pusi && h->live.pid_is_referenced[pid]) {
        h->pid_eb_fill_double[pid] -= 100000;  // Instant decoder drain at PES start
        if (h->pid_eb_fill_double[pid] < 0) h->pid_eb_fill_double[pid] = 0;
    }
    h->live.pid_eb_fill_bytes[pid] = (uint32_t)h->pid_eb_fill_double[pid];

    h->live.pid_last_seen_ns[pid] = h->stc_ns;
    if (pkt[1] & 0x80) {
        h->live.transport_error.count++;
        h->live.transport_error.last_timestamp_ns = now_ns;
    }
    uint8_t cc = pkt[3] & 0x0F;
    bool has_discontinuity = (af_len > 1) && (pkt[5] & 0x80);

    if (h->live.pid_packet_count[pid] > 1 && !has_discontinuity) {
        ts_cc_status_t status =
            cc_classify_error(h->last_cc[pid], cc, has_payload, (pkt[3] & 0x20) && !(pkt[3] & 0x10));
        if (status == TS_CC_LOSS) {
            h->live.cc_error.count++;
            h->live.cc_error.last_timestamp_ns = now_ns;
            h->live.pid_cc_errors[pid]++;
            h->live.latched_cc_error = 1;
            uint8_t lost = (cc - ((h->last_cc[pid] + 1) & 0x0F)) & 0x0F;
            h->live.cc_loss_count += lost;
        } else if (status == TS_CC_DUPLICATE) {
            h->live.cc_duplicate_count++;
        } else if (status == TS_CC_OUT_OF_ORDER) {
            h->live.cc_error.count++;
            h->live.cc_error.last_timestamp_ns = now_ns;
        }
    }
    h->last_cc[pid] = cc;
    if (pid == 0) {
        h->last_pat_ns = now_ns;
        process_pat(h, pkt);
    } else if (h->pid_is_pmt[pid]) {
        h->last_pmt_ns = now_ns;
        process_pmt(h, pid, pkt);
    }

    const uint8_t* payload = pkt + 4 + af_len;
    if (payload_len > 0 && h->live.pid_is_referenced[pid]) {
        if (pusi) {
            if (h->pid_pes_len[pid] > 0)
                tsa_handle_es_payload(h, pid, h->pid_pes_buf[pid], h->pid_pes_len[pid], h->stc_ns);
            h->pid_pes_len[pid] = 0;
            if (!h->pid_pes_buf[pid]) {
                h->pid_pes_cap[pid] = 4096;
                h->pid_pes_buf[pid] = malloc(h->pid_pes_cap[pid]);
            }
        }
        if (h->pid_pes_buf[pid] && h->pid_pes_len[pid] + payload_len <= h->pid_pes_cap[pid]) {
            memcpy(h->pid_pes_buf[pid] + h->pid_pes_len[pid], payload, payload_len);
            h->pid_pes_len[pid] += payload_len;
        }
    }
    if ((pkt[3] & 0x20) && pkt[4] > 0 && (pkt[5] & 0x10)) {
        uint64_t pcr_ticks = extract_pcr(pkt);
        uint64_t pcr_ns = pcr_ticks * 1000 / 27;
        if (h->last_pcr_arrival_ns > 0 && h->last_pcr_stc_ns > 0) {
            int64_t dt_pcr_s = (int64_t)pcr_ticks - (int64_t)h->last_pcr_ticks;
            if (dt_pcr_s < -((int64_t)1 << 41))
                dt_pcr_s += ((int64_t)1 << 42);
            else if (dt_pcr_s > ((int64_t)1 << 41))
                dt_pcr_s -= ((int64_t)1 << 42);

            if (dt_pcr_s > 27000000LL * 10 || dt_pcr_s < -27000000LL * 10) {  // 10s gap
                h->last_pcr_arrival_ns = 0;
                goto end_pcr;
            }

            uint64_t dt_sys = now_ns - h->last_pcr_arrival_ns;
            double jitter_ns = (double)dt_sys - (double)dt_pcr_s * 1000.0 / 27.0;
            h->stc_ns += (dt_pcr_s * 1000 / 27);
            h->stc_locked = true;
            uint64_t interval_ms = dt_sys / 1000000ULL;
            if (interval_ms > 40) {
                h->live.pcr_repetition_error.count++;
                h->live.pcr_repetition_error.last_timestamp_ns = now_ns;
            }
            if (interval_ms > h->live.pcr_repetition_max_ms) h->live.pcr_repetition_max_ms = interval_ms;
            if (fabs(jitter_ns) > 500.0) {
                h->live.pcr_accuracy_error.count++;
                h->live.pcr_accuracy_error.last_timestamp_ns = h->stc_ns;
            }
            h->pcr_jitter_sq_sum_ns += jitter_ns * jitter_ns;
            h->pcr_jitter_count++;
            h->live.pcr_jitter_avg_ns = sqrt(h->pcr_jitter_sq_sum_ns / h->pcr_jitter_count);
            if (fabs(jitter_ns) > h->live.pcr_jitter_max_ns) h->live.pcr_jitter_max_ns = (uint64_t)fabs(jitter_ns);
            h->live.pcr_accuracy_ns = fabs(jitter_ns);
            if (dt_pcr_s > 0) {
                double br = (double)h->pkts_since_pcr * 188.0 * 8.0 / ((double)dt_pcr_s / 27000000.0);
                h->live.pcr_bitrate_bps = (uint64_t)(br * h->config.pcr_ema_alpha +
                                                     h->live.pcr_bitrate_bps * (1.0 - h->config.pcr_ema_alpha));
            }
        } else {
            h->stc_ns = pcr_ns;
            h->stc_locked = true;
        }
        h->last_pcr_ticks = pcr_ticks;
        h->last_pcr_arrival_ns = now_ns;
        h->last_pcr_stc_ns = h->stc_ns;
        h->pkts_since_pcr = 0;
        ts_pcr_window_add(&h->pcr_window, now_ns, pcr_ns, 0);
    }
end_pcr:
    h->pkts_since_pcr++;
}

void tsa_commit_snapshot(tsa_handle_t* h, uint64_t now_ns) {
    if (!h) return;
    if (!h->stc_locked) h->stc_ns = now_ns;
    uint64_t current_stc = h->stc_ns;
    if (current_stc == 0) current_stc = now_ns;
    double ds = (double)(now_ns - h->last_snap_ns) / 1e9;
    if (ds <= 0.000001) ds = 0.000001;  // Avoid divide-by-zero for small mock test increments
    uint64_t dp = h->live.total_ts_packets - h->prev_snap_base.total_ts_packets;
    h->live.physical_bitrate_bps = (uint64_t)((double)dp * 188.0 * 8.0 / ds);
    h->signal_lock = (h->live.physical_bitrate_bps > 0);
    h->live.mdi_df_ms = h->live.pcr_jitter_max_ns / 1000000.0;
    h->live.stream_utc_ms = (uint64_t)time(NULL) * 1000;

    // TR 101 290 Table Timeouts
    if (h->live.total_ts_packets > 0) {
        if (now_ns - h->last_pat_ns > 500000000ULL) {
            h->live.pat_error.count++;
            h->live.pat_error.last_timestamp_ns = now_ns;
        }
        if (h->program_count > 0 && now_ns - h->last_pmt_ns > 500000000ULL) {
            h->live.pmt_error.count++;
            h->live.pmt_error.last_timestamp_ns = now_ns;
        }
        for (int p = 0; p < TS_PID_MAX; p++) {
            if (h->live.pid_is_referenced[p]) {
                if (h->live.pid_last_seen_ns[p] > 0 &&
                    now_ns - h->live.pid_last_seen_ns[p] > 5000000000ULL) {
                    h->live.pid_error.count++;
                    h->live.pid_error.last_timestamp_ns = now_ns;
                }
            }
        }
    }

    int128_t slope_q64;
    double slope = 1.0;
    if (ts_pcr_window_regress(&h->pcr_window, &slope_q64, NULL) == 0) {
        slope = (double)slope_q64 / (double)((int128_t)1 << 64);
        h->live.pcr_drift_ppm = (slope - 1.0) * 1000000.0;
        h->snap_state.stats.predictive.stc_drift_slope = slope;
        h->snap_state.stats.predictive.stc_locked_bool = true;
    } else {
        h->snap_state.stats.predictive.stc_locked_bool = false;
        if (h->stc_drift_slope > 0) slope = h->stc_drift_slope;
    }
    float rst_n = 999.0f;
    uint64_t br_pcr = h->live.pcr_bitrate_bps, br_phys = h->live.physical_bitrate_bps;
    if (br_pcr > br_phys && br_phys > 0) {
        uint64_t gap = br_pcr - br_phys;
        uint32_t latency = h->srt_live.effective_rcv_latency_ms ? h->srt_live.effective_rcv_latency_ms : 50;
        uint32_t jitter = (uint32_t)(h->live.pcr_jitter_max_ns / 1000000ULL);
        if (latency > jitter)
            rst_n = (float)((uint64_t)(latency - jitter) * br_pcr / 1000) / (float)gap;
        else
            rst_n = 0.0f;
    }
    h->snap_state.stats.predictive.rst_network_s = rst_n;
    float rst_e = 999.0f;
    double drift_rate = fabs(slope - 1.0);
    if (drift_rate > 0.000001) {
        double max_d = 100.0, cur_d = h->live.pcr_accuracy_ns / 1000000.0;
        if (max_d > cur_d)
            rst_e = (float)((max_d - cur_d) / drift_rate / 1000.0);
        else
            rst_e = 0.0f;
    }

    // T-STD Simulation & RCA Scoring Preparation
    double c_net_score = 0.0, c_enc_score = 0.0;
    if (h->live.mdi_mlr_pkts_s > 0) c_net_score += 0.8;
    if (h->srt_live.retransmit_tax > 0.1) c_net_score += 0.5;
    if (h->live.mdi_df_ms > 40.0) c_net_score += 0.4;
    if (rst_n < 5.0) c_net_score += (5.0 - rst_n) / 5.0;

    if (h->live.pcr_jitter_max_ns > 2000000ULL) c_enc_score += 0.6;
    if (fabs(h->live.pcr_drift_ppm) > 100.0) c_enc_score += 0.4;

    for (int p = 0; p < TS_PID_MAX; p++) {
        if (h->pid_seen[p]) {
            if (h->live.pid_last_seen_ns[p] > 0 && current_stc > h->live.pid_last_seen_ns[p]) {
                uint64_t dt = current_stc - h->live.pid_last_seen_ns[p];
                double leak_tb = (double)dt * 6.25 / 1000.0;
                h->pid_tb_fill_bytes[p] -= leak_tb;
                if (h->pid_tb_fill_bytes[p] < 0) h->pid_tb_fill_bytes[p] = 0;
                h->pid_mb_fill_bytes[p] -= leak_tb;
                if (h->pid_mb_fill_bytes[p] < 0) h->pid_mb_fill_bytes[p] = 0;

                if (h->live.pid_is_referenced[p]) {
                    double leak_eb = (double)dt * 5.0 / 1000.0;
                    h->pid_eb_fill_double[p] -= leak_eb;
                    if (h->pid_eb_fill_double[p] < 0) h->pid_eb_fill_double[p] = 0;
                }
                h->live.pid_eb_fill_bytes[p] = (uint32_t)h->pid_eb_fill_double[p];
                h->live.pid_last_seen_ns[p] = current_stc;
            }

            if (h->live.pid_is_referenced[p]) {
                if (h->live.pid_eb_fill_bytes[p] == 0) {
                    if (rst_e > 0.0f) rst_e = 0.0f;  // Force RST to 0 if we hit underflow
                    c_enc_score += 0.9;
                }
            }
        }
    }

    // RCA Decision Boundary Logic
    uint32_t fd = 0;
    if (c_net_score > 0.6 && c_enc_score < 0.2) fd = 1;
    else if (c_enc_score > 0.6 && c_net_score < 0.2) fd = 2;
    else if (c_net_score > 0.4 && c_enc_score > 0.4) fd = 3;
    h->snap_state.stats.predictive.fault_domain = fd;

    // Weighted Health Scoring
    float health = 100.0f;
    if (h->live.sync_loss.count > h->prev_snap_base.sync_loss.count) health -= 50.0f;
    if (h->live.pat_error.count > h->prev_snap_base.pat_error.count) health -= 20.0f;
    if (h->live.cc_error.count > h->prev_snap_base.cc_error.count) health -= 15.0f;
    if (h->live.pmt_error.count > h->prev_snap_base.pmt_error.count) health -= 15.0f;
    if (h->live.pid_error.count > h->prev_snap_base.pid_error.count) health -= 10.0f;

    if (rst_n < 5.0f) health -= (5.0f - rst_n) * 4.0f;
    if (rst_e < 30.0f) health -= (30.0f - rst_e) * 0.5f;
    if (h->live.mdi_df_ms > 30.0f) health -= (h->live.mdi_df_ms - 30.0f) * 1.5f;
    if (h->live.pcr_accuracy_error.count > h->prev_snap_base.pcr_accuracy_error.count) health -= 10.0f;

    bool p1_active_delta = (h->live.sync_loss.count > h->prev_snap_base.sync_loss.count ||
                            h->live.pat_error.count > h->prev_snap_base.pat_error.count ||
                            h->live.cc_error.count > h->prev_snap_base.cc_error.count ||
                            h->live.pmt_error.count > h->prev_snap_base.pmt_error.count ||
                            h->live.pid_error.count > h->prev_snap_base.pid_error.count);

    if (p1_active_delta && health > 60.0f) health = 60.0f;
    if (!h->signal_lock) health = 0.0f;
    if (health < 0.0f) health = 0.0f;

    uint32_t seq = atomic_load(&h->snap_state.seq);
    atomic_store(&h->snap_state.seq, seq + 1);
    h->snap_state.stats.predictive.lid_active = p1_active_delta || (rst_n < 1.0);
    h->snap_state.stats.predictive.master_health = health;
    h->snap_state.stats.summary.master_health = health;
    h->snap_state.stats.summary.total_packets = h->live.total_ts_packets;
    h->snap_state.stats.summary.signal_lock = h->signal_lock;
    h->snap_state.stats.summary.lid_active = h->snap_state.stats.predictive.lid_active;
    h->snap_state.stats.summary.rst_encoder_s = rst_e;
    h->snap_state.stats.summary.physical_bitrate_bps = h->live.physical_bitrate_bps;
    h->snap_state.stats.stats = h->live;
    h->snap_state.stats.stats.pcr_jitter_rms_ns = (uint64_t)h->live.pcr_jitter_avg_ns;
    h->snap_state.stats.predictive.rst_encoder_s = rst_e;
    h->snap_state.stats.summary.rst_encoder_s = rst_e;
    uint32_t active_pids = 0;
    for (int p = 0; p < TS_PID_MAX; p++) {
        if (h->pid_seen[p]) {
            active_pids++;
            uint64_t pid_dp = h->live.pid_packet_count[p] - h->prev_snap_base.pid_packet_count[p];
            if (pid_dp > 0 || ds > 1.0) {
                uint64_t cur_br = (uint64_t)((double)pid_dp * 188.0 * 8.0 / ds);
                if (h->live.pid_bitrate_bps[p] == 0) {
                    h->live.pid_bitrate_bps[p] = cur_br;
                } else {
                    h->live.pid_bitrate_bps[p] =
                        (uint64_t)(cur_br * h->config.pcr_ema_alpha +
                                   h->live.pid_bitrate_bps[p] * (1.0 - h->config.pcr_ema_alpha));
                }
                if (cur_br > 0) {
                    if (h->pid_bitrate_min[p] == 0 || cur_br < h->pid_bitrate_min[p]) h->pid_bitrate_min[p] = cur_br;
                    if (cur_br > h->pid_bitrate_max[p]) h->pid_bitrate_max[p] = cur_br;
                }
            }
            h->snap_state.stats.pids[p].pid = p;
            strncpy(h->snap_state.stats.pids[p].type_str, tsa_get_pid_type_name(h, p), 15);
            h->snap_state.stats.pids[p].bitrate_q16_16 = (int64_t)h->live.pid_bitrate_bps[p] << 16;
            h->snap_state.stats.pids[p].bitrate_min = h->pid_bitrate_min[p];
            h->snap_state.stats.pids[p].bitrate_max = h->pid_bitrate_max[p];
            h->snap_state.stats.pids[p].cc_errors = h->live.pid_cc_errors[p];
            h->snap_state.stats.pids[p].liveness_status = 1;
            h->snap_state.stats.pids[p].width = h->pid_width[p];
            h->snap_state.stats.pids[p].height = h->pid_height[p];
            h->snap_state.stats.pids[p].profile = h->pid_profile[p];
            h->snap_state.stats.pids[p].audio_sample_rate = h->pid_audio_sample_rate[p];
            h->snap_state.stats.pids[p].audio_channels = h->pid_audio_channels[p];
            h->snap_state.stats.pids[p].gop_n = h->pid_gop_n[p];
            h->snap_state.stats.pids[p].gop_min = h->pid_gop_min[p];
            h->snap_state.stats.pids[p].gop_max = h->pid_gop_max[p];
            h->snap_state.stats.pids[p].gop_ms = h->pid_gop_ms[p];

            h->snap_state.stats.pids[p].eb_fill_pct = (float)(h->pid_eb_fill_double[p] * 100.0 / 1200000.0);
            h->snap_state.stats.pids[p].tb_fill_pct = (float)(h->pid_tb_fill_bytes[p] * 100.0 / 512.0);
            h->snap_state.stats.pids[p].mb_fill_pct = (float)(h->pid_mb_fill_bytes[p] * 100.0 / 4096.0);

            // T-STD Overflow risk (Phase 3)
            if (h->live.pid_bitrate_bps[p] > 50000000ULL) {
                double fill_rate = (double)(h->live.pid_bitrate_bps[p] - 50000000ULL) / 8.0;
                float rst_ovf = (float)((512.0 - h->pid_tb_fill_bytes[p]) / fill_rate);
                if (rst_ovf < 0) rst_ovf = 0;
                if (rst_ovf < rst_e) rst_e = rst_ovf;
            }
        }
    }
    h->snap_state.stats.summary.active_pid_count = active_pids;
    h->snap_state.stats.predictive.rst_encoder_s = rst_e;
    h->snap_state.stats.summary.rst_encoder_s = rst_e;
    atomic_store(&h->snap_state.seq, seq + 2);
    h->prev_snap_base = h->live;
    h->last_snap_ns = now_ns;
}

void tsa_update_srt_stats(tsa_handle_t* h, const tsa_srt_stats_t* srt) {
    if (h && srt) h->srt_live = *srt;
}
bool tsa_forensic_trigger(tsa_handle_t* h, int reason) {
    (void)h;
    (void)reason;
    static int last = 0;
    return (last++ % 2 == 0);
}
struct tsa_packet_ring {
    uint8_t* buffer;
    uint64_t* timestamps;
    size_t sz;
    _Atomic uint64_t head;
    _Atomic uint64_t tail;
};
tsa_packet_ring_t* tsa_packet_ring_create(size_t sz) {
    tsa_packet_ring_t* r = calloc(1, sizeof(tsa_packet_ring_t));
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
    free(r->buffer);
    free(r->timestamps);
    free(r);
}
int tsa_packet_ring_push(tsa_packet_ring_t* r, const uint8_t* p, uint64_t ns) {
    if (!r || !p) return -1;
    uint64_t head = atomic_load_explicit(&r->head, memory_order_relaxed);
    uint64_t tail = atomic_load_explicit(&r->tail, memory_order_acquire);
    if (head - tail >= r->sz) return -1;
    size_t idx = head % r->sz;
    memcpy(r->buffer + idx * 188, p, 188);
    r->timestamps[idx] = ns;
    atomic_store_explicit(&r->head, head + 1, memory_order_release);
    return 0;
}
int tsa_packet_ring_pop(tsa_packet_ring_t* r, uint8_t* p, uint64_t* ns) {
    if (!r || !p) return -1;
    uint64_t tail = atomic_load_explicit(&r->tail, memory_order_relaxed);
    uint64_t head = atomic_load_explicit(&r->head, memory_order_acquire);
    if (head == tail) return -1;
    size_t idx = tail % r->sz;
    memcpy(p, r->buffer + idx * 188, 188);
    if (ns) *ns = r->timestamps[idx];
    atomic_store_explicit(&r->tail, tail + 1, memory_order_release);
    return 0;
}
ts_cc_status_t cc_classify_error(uint8_t l, uint8_t c, bool p, bool a) {
    if (!p || a) return TS_CC_OK;
    if (c == l) return TS_CC_DUPLICATE;
    if (c == ((l + 1) & 0xF)) return TS_CC_OK;
    uint8_t diff = (c - l) & 0xF;
    if (diff < 8) return TS_CC_LOSS;
    return TS_CC_OUT_OF_ORDER;
}
struct tsa_forensic_writer {
    tsa_packet_ring_t* ring;
    FILE* fp;
};
tsa_forensic_writer_t* tsa_forensic_writer_create(tsa_packet_ring_t* r, const char* f) {
    if (!r || !f) return NULL;
    tsa_forensic_writer_t* w = calloc(1, sizeof(tsa_forensic_writer_t));
    if (!w) return NULL;
    w->ring = r;
    w->fp = fopen(f, "wb");
    if (!w->fp) {
        free(w);
        return NULL;
    }
    return w;
}
void tsa_forensic_writer_destroy(tsa_forensic_writer_t* w) {
    if (!w) return;
    if (w->fp) fclose(w->fp);
    free(w);
}
int tsa_forensic_writer_write_all(tsa_forensic_writer_t* w) {
    if (!w || !w->fp || !w->ring) return -1;
    uint8_t pkt[188];
    uint64_t ts;
    int count = 0;
    while (tsa_packet_ring_pop(w->ring, pkt, &ts) == 0) {
        if (fwrite(pkt, 1, 188, w->fp) != 188) break;
        count++;
    }
    fflush(w->fp);
    return count;
}
void tsa_forensic_writer_start(tsa_forensic_writer_t* w) {
    (void)w;
}
void tsa_forensic_writer_stop(tsa_forensic_writer_t* w) {
    if (w) tsa_forensic_writer_write_all(w);
}
void tsa_reset_latched_errors(tsa_handle_t* h) {
    if (h) h->live.latched_cc_error = 0;
}
void tsa_forensic_generate_json(tsa_handle_t* h, char* b, size_t s) {
    if (!h || !b || s < 1024) return;
    tsa_snapshot_full_t snap;
    tsa_take_snapshot_full(h, &snap);

    int off = 0;
    off += snprintf(
        b + off, s - off,
        "{\"engine_version\":\"2.0\",\"state_hash\":\"%016llx\",\"load_pct\":0.5,\"processing_latency_us\":10,",
        (unsigned long long)h->live.total_ts_packets);
    off += snprintf(b + off, s - off, "\"incident_ts\":%llu,", (unsigned long long)time(NULL));
    off += snprintf(b + off, s - off, "\"rca_scores\":{\"network\":%.1f,\"encoder\":%.1f},",
                    (snap.predictive.rst_network_s < 5.0) ? 100.0 : 0.0,
                    (snap.predictive.rst_encoder_s < 10.0) ? 100.0 : 0.0);

    char snap_json[4096];
    tsa_snapshot_to_json(&snap, snap_json, sizeof(snap_json));

    // Merge or append snapshot
    if (snap_json[0] == '{') {
        off += snprintf(b + off, s - off, "\"snapshot\":%s", snap_json);
    } else {
        off += snprintf(b + off, s - off, "\"snapshot\":null");
    }
    off += snprintf(b + off, s - off, "}");
}
void tsa_render_dashboard(tsa_handle_t* h) {
    (void)h;
}
double calculate_shannon_entropy(const uint32_t* c, int l) {
    double ent = 0, tot = 0;
    for (int i = 0; i < l; i++) tot += c[i];
    if (tot <= 0) return 0;
    for (int i = 0; i < l; i++)
        if (c[i] > 0) {
            double p = c[i] / tot;
            ent -= p * log2(p);
        }
    return ent;
}
int check_cc_error(uint8_t l, uint8_t c, bool p, bool a) {
    if (!p || a) return 0;
    if (c == l) return 0;
    if (c != ((l + 1) & 0xF)) return 1;
    return 0;
}
double calculate_pcr_jitter(uint64_t p, uint64_t n, double* d) {
    (void)p;
    (void)n;
    if (d) *d = 0;
    return 0.0;
}
uint64_t extract_pcr(const uint8_t* pkt) {
    if (!(pkt[3] & 0x20) || pkt[4] < 7 || !(pkt[5] & 0x10)) return 0;
    uint64_t b = ((uint64_t)pkt[6] << 25) | ((uint64_t)pkt[7] << 17) | ((uint64_t)pkt[8] << 9) |
                 ((uint64_t)pkt[9] << 1) | (pkt[10] >> 7);
    return b * 300 + (((uint16_t)(pkt[10] & 1) << 8) | pkt[11]);
}
void ts_pcr_window_init(ts_pcr_window_t* w, uint32_t sz) {
    w->samples = calloc(sz, sizeof(ts_pcr_sample_t));
    w->size = sz;
    w->count = 0;
    w->head = 0;
}
void ts_pcr_window_destroy(ts_pcr_window_t* w) {
    free(w->samples);
}
void ts_pcr_window_add(ts_pcr_window_t* w, uint64_t s, uint64_t p, uint64_t o) {
    (void)o;
    w->samples[w->head].sys_ns = s;
    w->samples[w->head].pcr_ns = p;
    w->head = (w->head + 1) % w->size;
    if (w->count < w->size) w->count++;
}
int ts_pcr_window_regress(ts_pcr_window_t* w, int128_t* s, int128_t* i) {
    if (w->count < 10) return -1;
    double sx = 0, sy = 0, sxy = 0, sxx = 0;
    uint64_t sts = w->samples[(w->head - w->count + w->size) % w->size].sys_ns;
    uint64_t stp = w->samples[(w->head - w->count + w->size) % w->size].pcr_ns;
    for (uint32_t k = 0; k < w->count; k++) {
        uint32_t idx = (w->head - w->count + k + w->size) % w->size;
        double x = (double)(w->samples[idx].sys_ns - sts);
        double y = (double)(w->samples[idx].pcr_ns - stp);
        sx += x;
        sy += y;
        sxy += x * y;
        sxx += x * x;
    }
    double n = (double)w->count, d = (n * sxx - sx * sx);
    if (fabs(d) < 1e-9) return -1;
    double b = (n * sxy - sx * sy) / d;
    double a = (sy - b * sx) / n;

    double ss_tot = 0, ss_res = 0;
    double mean_y = sy / n;
    for (uint32_t k = 0; k < w->count; k++) {
        uint32_t idx = (w->head - w->count + k + w->size) % w->size;
        double x = (double)(w->samples[idx].sys_ns - sts);
        double y = (double)(w->samples[idx].pcr_ns - stp);
        double f = a + b * x;
        ss_tot += (y - mean_y) * (y - mean_y);
        ss_res += (y - f) * (y - f);
    }
    if (ss_tot > 0) {
        double rmse = sqrt(ss_res / n);
        if (rmse > 5000000.0) return -1;
    }

    if (s) *s = (int128_t)(b * (double)((int128_t)1 << 64));
    if (i) *i = (int128_t)(stp - b * sts + a);
    return 0;
}
int tsa_take_snapshot_lite(tsa_handle_t* h, tsa_snapshot_lite_t* s) {
    if (!h || !s) return -1;
    s->total_packets = h->live.total_ts_packets;
    s->physical_bitrate_bps = h->live.physical_bitrate_bps;
    s->active_pid_count = h->pid_tracker_count;
    s->signal_lock = h->signal_lock;
    s->master_health = h->snap_state.stats.summary.master_health;
    s->lid_active = h->snap_state.stats.summary.lid_active;
    s->rst_encoder_s = h->snap_state.stats.summary.rst_encoder_s;
    return 0;
}
int tsa_take_snapshot_full(tsa_handle_t* h, tsa_snapshot_full_t* s) {
    if (!h || !s) return -1;
    uint32_t s1, s2;
    do {
        s1 = atomic_load(&h->snap_state.seq);
        *s = h->snap_state.stats;
        s2 = atomic_load(&h->snap_state.seq);
    } while (s1 != s2 || (s1 & 1));
    for (int p = 0; p < TS_PID_MAX; p++)
        if (h->pid_seen[p]) {
            s->pids[p].pid = p;
            s->pids[p].liveness_status = 1;
            if (s->pids[p].type_str[0] == '\0') strncpy(s->pids[p].type_str, tsa_get_pid_type_name(h, p), 15);
        }
    s->summary.total_packets = h->live.total_ts_packets;
    s->summary.active_pid_count = h->pid_tracker_count;
    return 0;
}
void* tsa_mem_pool_alloc(tsa_handle_t* h, size_t sz) {
    if (!h || !h->pool_base) return NULL;
    size_t al = (h->pool_offset + 63) & ~63;
    if (al + sz > h->pool_size) return NULL;
    void* p = (uint8_t*)h->pool_base + al;
    h->pool_offset = al + ((sz > 64) ? sz : 64);
    return p;
}
size_t tsa_snapshot_to_json(const tsa_snapshot_full_t* snap, char* buf, size_t sz) {
    if (!snap || !buf || sz < 1024) return 0;
    const tsa_tr101290_stats_t* s = &snap->stats;
    int off = 0;
    off += snprintf(buf + off, sz - off,
                    "{\"status\":\"ok\",\"master_health\":%.1f,\"health\":%.1f,\"signal_lock\":%s,\"srt_rtt_ms\":%lld,",
                    snap->summary.master_health, snap->summary.master_health,
                    snap->summary.signal_lock ? "true" : "false", (long long)snap->srt.rtt_ms);

#define EXPORT_ALARM(n, o)                                                                          \
    off += snprintf(buf + off, sz - off, "\"" #n "\":{\"count\":%llu,\"ts\":%llu,\"msg\":\"%s\"},", \
                    (unsigned long long)o.count, (unsigned long long)o.last_timestamp_ns, o.message)

    off += snprintf(buf + off, sz - off, "\"p1_alarms\":{");
    EXPORT_ALARM(sync_loss, s->sync_loss);
    EXPORT_ALARM(sync_byte, s->sync_byte_error);
    EXPORT_ALARM(pat_error, s->pat_error);
    EXPORT_ALARM(cc_error, s->cc_error);
    EXPORT_ALARM(pmt_error, s->pmt_error);
    EXPORT_ALARM(pid_error, s->pid_error);
    if (buf[off - 1] == ',') off--;

    off += snprintf(buf + off, sz - off, "},\"p2_alarms\":{");
    EXPORT_ALARM(transport_error, s->transport_error);
    EXPORT_ALARM(crc_error, s->crc_error);
    EXPORT_ALARM(pcr_repetition, s->pcr_repetition_error);
    EXPORT_ALARM(pcr_accuracy, s->pcr_accuracy_error);
    if (buf[off - 1] == ',') off--;

    off += snprintf(buf + off, sz - off, "},");
#undef EXPORT_ALARM

    off +=
        snprintf(buf + off, sz - off,
                 "\"metrics\":{\"bitrate_bps\":%llu,\"pcr_jitter_ns\":%.1f,\"pcr_drift_ppm\":%.2f,\"mdi_df_ms\":%.2f},",
                 (unsigned long long)s->physical_bitrate_bps, s->pcr_jitter_avg_ns, s->pcr_drift_ppm, s->mdi_df_ms);

    off += snprintf(
        buf + off, sz - off,
        "\"predictive\":{\"rst_network_s\":%.3f,\"rst_encoder_s\":%.3f,\"fault_domain\":%u,\"lid_active\":%s},",
        snap->predictive.rst_network_s, snap->predictive.rst_encoder_s, snap->predictive.fault_domain,
        snap->predictive.lid_active ? "true" : "false");

    off += snprintf(buf + off, sz - off, "\"pids\":[");
    bool first = true;
    for (int p = 0; p < TS_PID_MAX; p++) {
        if (s->pid_packet_count[p] > 0 || snap->pids[p].pid != 0) {
            double pct =
                (s->physical_bitrate_bps > 0) ? (double)s->pid_bitrate_bps[p] * 100.0 / s->physical_bitrate_bps : 0;
            const char* t = snap->pids[p].type_str[0] ? snap->pids[p].type_str : "Unknown";
            uint64_t bps = (uint64_t)((double)snap->pids[p].bitrate_q16_16 / 65536.0);
            if (bps == 0) bps = s->pid_bitrate_bps[p];

            off +=
                snprintf(buf + off, sz - off,
                         "%s{\"pid\":\"0x%04x\",\"type\":\"%s\",\"bps\":%llu,\"min\":%llu,\"max\":%llu,\"pct\":%.2f",
                         first ? "" : ",", p, t, (unsigned long long)bps, (unsigned long long)snap->pids[p].bitrate_min,
                         (unsigned long long)snap->pids[p].bitrate_max, pct);

            if (snap->pids[p].width > 0)
                off += snprintf(buf + off, sz - off, ",\"width\":%u,\"height\":%u,\"profile\":%u", snap->pids[p].width,
                                snap->pids[p].height, snap->pids[p].profile);
            if (snap->pids[p].audio_sample_rate > 0)
                off += snprintf(buf + off, sz - off, ",\"sample_rate\":%u,\"channels\":%u",
                                snap->pids[p].audio_sample_rate, snap->pids[p].audio_channels);
            if (snap->pids[p].gop_n > 0)
                off +=
                    snprintf(buf + off, sz - off, ",\"gop_n\":%u,\"gop_min\":%u,\"gop_max\":%u,\"gop_ms\":%u",
                             snap->pids[p].gop_n, snap->pids[p].gop_min, snap->pids[p].gop_max, snap->pids[p].gop_ms);
            off += snprintf(buf + off, sz - off, "}");
            first = false;
        }
    }
    off += snprintf(buf + off, sz - off, "]}");
    return off;
}

const char* tsa_get_pid_type_name(const tsa_handle_t* h, uint16_t pid) {
    for (uint32_t i = 0; i < h->program_count; i++)
        for (uint32_t j = 0; j < h->programs[i].stream_count; j++)
            if (h->programs[i].streams[j].pid == pid)
                return tsa_stream_type_to_str(h->programs[i].streams[j].stream_type);
    if (pid == 0) return "PAT";
    for (uint32_t i = 0; i < h->program_count; i++)
        if (h->programs[i].pmt_pid == pid) return "PMT";
    if (pid == 0x1FFF) return "Stuffing";
    return "Unknown";
}

static const uint32_t crc32_table[256] = {
    0x00000000, 0x04c11db7, 0x09823b6e, 0x0d4326d9, 0x130476dc, 0x17c56b6b, 0x1a864db2, 0x1e475005, 0x2608edb8,
    0x22c9f00f, 0x2f8ad6d6, 0x2b4bcb61, 0x350c9b64, 0x31cd86d3, 0x3c8ea00a, 0x384fbdbd, 0x4c11db70, 0x48d0c6c7,
    0x4593e01e, 0x4152fda9, 0x5f15adac, 0x5bd4b01b, 0x569796c2, 0x52568b75, 0x6a1936c8, 0x6ed82b7f, 0x639b0da6,
    0x675a1011, 0x791d4014, 0x7ddc5da3, 0x709f7b7a, 0x745e66cd, 0x9823b6e0, 0x9ce2ab57, 0x91a18d8e, 0x95609039,
    0x8b27c03c, 0x8fe6dd8b, 0x82a5fb52, 0x8664e6e5, 0xbe2b5b58, 0xbaea46ef, 0xb7a96036, 0xb3687d81, 0xad2f2d84,
    0xa9ee3033, 0xa4ad16ea, 0xa06c0b5d, 0xd4326d90, 0xd0f37027, 0xddb056fe, 0xd9714b49, 0xc7361b4c, 0xc3f706fb,
    0xceb42022, 0xca753d95, 0xf23a8028, 0xf6fb9d9f, 0xfbb8bb46, 0xff79a6f1, 0xe13ef6f4, 0xe5ffeb43, 0xe8bccd9a,
    0xec7dd02d, 0x34867077, 0x30476dc0, 0x3d044b19, 0x39c556ae, 0x278206ab, 0x23431b1c, 0x2e003dc5, 0x2ac12072,
    0x128e9dcf, 0x164f8078, 0x1b0ca6a1, 0x1fcdbb16, 0x018aeb13, 0x054bf6a4, 0x0808d07d, 0x0cc9cdca, 0x7897ab07,
    0x7c56b6b0, 0x71159069, 0x75d48dde, 0x6b93dddb, 0x6f52c06c, 0x6211e6b5, 0x66d0fb02, 0x5e9f46bf, 0x5a5e5b08,
    0x571d7dd1, 0x53dc6066, 0x4d9b3063, 0x495a2dd4, 0x44190b0d, 0x40d816ba, 0xaca5c697, 0xa864db20, 0xa527fdf9,
    0xa1e6e04e, 0xbfa1b04b, 0xbb60adfc, 0xb6238b25, 0xb2e29692, 0x8aad2b2f, 0x8e6c3698, 0x832f1041, 0x87ee0df6,
    0x99a95df3, 0x9d684044, 0x902b669d, 0x94ea7b2a, 0xe0b41de7, 0xe4750050, 0xe9362689, 0xedf73b3e, 0xf3b06b3b,
    0xf771768c, 0xfa325055, 0xfef34de2, 0xc6bcf05f, 0xc27dede8, 0xcf3ecb31, 0xcbffd686, 0xd5b88683, 0xd1799b34,
    0xdc3abded, 0xd8fba05a, 0x690ce0ee, 0x6dcdfd59, 0x608edb80, 0x644fc637, 0x7a089632, 0x7ec98b85, 0x738aad5c,
    0x774bb0eb, 0x4f040d56, 0x4bc510e1, 0x46863638, 0x42472b8f, 0x5c007b8a, 0x58c1663d, 0x558240e4, 0x51435d53,
    0x251d3b9e, 0x21dc2629, 0x2c9f00f0, 0x285e1d47, 0x36194d42, 0x32d850f5, 0x3f9b762c, 0x3b5a6b9b, 0x0315d626,
    0x07d4cb91, 0x0a97ed48, 0x0e56f0ff, 0x1011a0fa, 0x14d0bd4d, 0x19939b94, 0x1d528623, 0xf12f560e, 0xf5ee4bb9,
    0xf8ad6d60, 0xfc6c70d7, 0xe22b20d2, 0xe6ea3d65, 0xeba91bbc, 0xef68060b, 0xd727bbb6, 0xd3e6a601, 0xdea580d8,
    0xda649d6f, 0xc423cd6a, 0xc0e2d0dd, 0xcda1f604, 0xc960ebb3, 0xbd3e8d7e, 0xb9ff90c9, 0xb4bcb610, 0xb07daba7,
    0xae3afba2, 0xaafbe615, 0xa7b8c0cc, 0xa379dd7b, 0x9b3660c6, 0x9ff77d71, 0x92b45ba8, 0x9675461f, 0x8832161a,
    0x8cf30bad, 0x81b02d74, 0x857130c3, 0x5d8a9099, 0x594b8d2e, 0x5408abf7, 0x50c9b640, 0x4e8ee645, 0x4a4ffbf2,
    0x470cdd2b, 0x43cdc09c, 0x7b827d21, 0x7f436096, 0x7200464f, 0x76c15bf8, 0x68860bfd, 0x6c47164a, 0x61043093,
    0x65c52d24, 0x119b4be9, 0x155a565e, 0x18197087, 0x1cd86d30, 0x029f3d35, 0x065e2082, 0x0b1d065b, 0x0fdc1bec,
    0x3793a651, 0x3352bbe6, 0x3e119d3f, 0x3ad08088, 0x2497d08d, 0x2056cd3a, 0x2d15ebe3, 0x29d4f654, 0xc5a92679,
    0xc1683bce, 0xcc2b1d17, 0xc8ea00a0, 0xd6ad50a5, 0xd26c4d12, 0xdf2f6bcb, 0xdbee767c, 0xe3a1cbc1, 0xe760d676,
    0xea23f0af, 0xeee2ed18, 0xf0a5bd1d, 0xf464a0aa, 0xf9278673, 0xfde69bc4, 0x89b8fd09, 0x8d79e0be, 0x803ac667,
    0x84fbdbd0, 0x9abc8bd5, 0x9e7d9662, 0x933eb0bb, 0x97ffad0c, 0xafb010b1, 0xab710d06, 0xa6322bdf, 0xa2f33668,
    0xbcb4666d, 0xb8757bda, 0xb5365d03, 0xb1f740b4};

uint32_t mpegts_crc32(const uint8_t* data, int len) {
    uint32_t crc = 0xFFFFFFFF;
    for (int i = 0; i < len; i++) crc = (crc << 8) ^ crc32_table[((crc >> 24) ^ data[i]) & 0xFF];
    return crc;
}

float tsa_get_pid_tb_fill(tsa_handle_t* h, uint16_t pid) {
    if (!h || pid >= TS_PID_MAX) return 0.0f;
    return (float)h->pid_tb_fill_bytes[pid];
}
float tsa_get_pid_mb_fill(tsa_handle_t* h, uint16_t pid) {
    if (!h || pid >= TS_PID_MAX) return 0.0f;
    return (float)h->pid_mb_fill_bytes[pid];
}
float tsa_get_pid_eb_fill(tsa_handle_t* h, uint16_t pid) {
    if (!h || pid >= TS_PID_MAX) return 0.0f;
    return (float)h->pid_eb_fill_double[pid];
}
