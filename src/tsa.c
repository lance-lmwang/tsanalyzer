#define _GNU_SOURCE
#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "tsa_internal.h"

/* --- Macros --- */
#define ALLOC_OR_GOTO(p, t, n)    \
    do {                          \
        p = calloc(n, sizeof(t)); \
        if (!(p)) goto fail;      \
    } while (0)

#define FREE_IF(p)      \
    do {                \
        if (p) free(p); \
    } while (0)

/* --- Forward Declarations --- */
static void tsa_reset_pid_stats(tsa_handle_t* h, uint16_t pid);
static int16_t tsa_update_pid_tracker(tsa_handle_t* h, uint16_t pid);

/* --- High-precision Time Utilities --- */
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

/* --- String Utilities --- */
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
    size_t rem = (b->offset < b->size) ? (b->size - b->offset) : 0;
    size_t cp = (len < rem) ? len : rem;
    if (cp > 0) {
        memcpy(b->ptr + b->offset, s, cp);
        b->offset += cp;
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

/* --- Bit Reader --- */
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

static void parse_ac3_audio(tsa_handle_t* h, uint16_t pid, const uint8_t* buf, int size) {
    if (size < 6 || buf[0] != 0x0B || buf[1] != 0x77) return;
    uint8_t fsc = (buf[4] >> 6) & 0x03, acm = buf[6] >> 5;
    uint32_t r[] = {48000, 44100, 32000, 0};
    uint8_t c[] = {2, 1, 2, 3, 3, 4, 4, 5};
    if (fsc < 3) h->pid_audio_sample_rate[pid] = r[fsc];
    if (acm < 8) h->pid_audio_channels[pid] = c[acm];
}

void tsa_handle_es_payload(tsa_handle_t* h, uint16_t pid, const uint8_t* pay, int len, uint64_t now) {
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
    for (int i = 0; i <= len - 4; i++) {
        if (pay[i] == 0x00 && pay[i + 1] == 0x00 && pay[i + 2] == 0x01) {
            uint8_t nt;
            const uint8_t* d = pay + i + 3;
            int l = len - (i + 3);
            if (l <= 0) continue;
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
            } else if (is_idr || (is_h264 && nt == 1) || (is_h265 && nt <= 21)) {
                if (l < 2) continue;
                bool is_nf = false;
                uint32_t hbt = 0xFFFFFFFF;
                if (is_h264) {
                    bit_reader_t br = {d + 1, l - 1, 0};
                    uint32_t fmb = read_ue(&br);
                    hbt = read_ue(&br);
                    read_ue(&br);
                    int fnb = h->pid_log2_max_frame_num[pid] ? h->pid_log2_max_frame_num[pid] : 4;
                    uint32_t fn = read_bits(&br, fnb);
                    if (!h->pid_frame_num_valid[pid]) {
                        is_nf = (fmb == 0);
                        h->pid_frame_num_valid[pid] = true;
                    } else if (fn != h->pid_last_frame_num[pid] || (fmb == 0 && is_idr))
                        is_nf = true;
                    h->pid_last_frame_num[pid] = fn;
                }
                if (is_idr) {
                    if (h->pid_last_idr_ns[pid] > 0) {
                        uint32_t cg = h->pid_gop_n[pid];
                        h->pid_last_gop_n[pid] = cg;
                        h->pid_gop_ms[pid] = (uint32_t)((now - h->pid_last_idr_ns[pid]) / 1000000ULL);
                        if (cg > 0) {
                            if (h->pid_gop_min[pid] == 0 || h->pid_gop_min[pid] == 0xFFFFFFFF ||
                                cg < h->pid_gop_min[pid])
                                h->pid_gop_min[pid] = cg;
                            if (cg > h->pid_gop_max[pid]) h->pid_gop_max[pid] = cg;
                        }
                    }
                    h->pid_last_idr_ns[pid] = now;
                    h->pid_gop_n[pid] = 1;
                    h->pid_i_frames[pid]++;
                } else if (is_nf) {
                    h->pid_gop_n[pid]++;
                    if (is_h264 && hbt != 0xFFFFFFFF) {
                        uint32_t b = (hbt >= 5) ? hbt - 5 : hbt;
                        if (b == 0 || b == 3)
                            h->pid_p_frames[pid]++;
                        else if (b == 1)
                            h->pid_b_frames[pid]++;
                    }
                }
            }
        }
    }
}

static void process_pat(tsa_handle_t* h, const uint8_t* pkt, uint64_t now) {
    uint8_t af = (pkt[3] & 0x20) ? pkt[4] + 1 : 0;
    const uint8_t* p = pkt + 4 + af;
    p += 1 + p[0];
    if (p >= pkt + 188) return;
    int sl = ((p[1] & 0x0F) << 8) | p[2];
    if (p + 3 + sl > pkt + 188) return;
    if (mpegts_crc32(p, sl + 3) != 0) {
        h->live->crc_error.count++;
        h->live->crc_error.last_timestamp_ns = now;
        return;
    }
    h->seen_pat = true;
    h->program_count = 0;
    for (int i = 8; i < sl + 3 - 4; i += 4) {
        uint16_t pn = (p[i] << 8) | p[i + 1], pp = ((p[i + 2] & 0x1F) << 8) | p[i + 3];
        if (pn != 0 && h->program_count < MAX_PROGRAMS) {
            h->pid_is_pmt[pp] = true;
            h->live->pid_is_referenced[pp] = true;
            h->programs[h->program_count].pmt_pid = pp;
            h->programs[h->program_count].stream_count = 0;
            h->program_count++;
        }
    }
}

static void process_pmt(tsa_handle_t* h, uint16_t pmt, const uint8_t* pkt, uint64_t now) {
    uint8_t af = (pkt[3] & 0x20) ? pkt[4] + 1 : 0;
    const uint8_t* p = pkt + 4 + af;
    p += 1 + p[0];
    if (p >= pkt + 188) return;
    int sl = ((p[1] & 0x0F) << 8) | p[2];
    if (p + 3 + sl > pkt + 188) return;
    if (mpegts_crc32(p, sl + 3) != 0) {
        h->live->crc_error.count++;
        h->live->crc_error.last_timestamp_ns = now;
        return;
    }
    uint16_t pcr = ((p[8] & 0x1F) << 8) | p[9];
    int pi = ((p[10] & 0x0F) << 8) | p[11];
    ts_program_info_t* pr = NULL;
    for (uint32_t i = 0; i < h->program_count; i++)
        if (h->programs[i].pmt_pid == pmt) {
            pr = &h->programs[i];
            break;
        }
    if (pr) {
        for (uint32_t i = 0; i < pr->stream_count; i++) {
            h->live->pid_is_referenced[pr->streams[i].pid] = false;
            tsa_reset_pid_stats(h, pr->streams[i].pid);
        }
        pr->pcr_pid = pcr;
        if (!h->live->pid_is_referenced[pcr]) tsa_reset_pid_stats(h, pcr);
        h->live->pid_is_referenced[pcr] = true;
        pr->stream_count = 0;
    }
    for (int i = 12 + pi; i < sl + 3 - 4;) {
        uint8_t ty = p[i];
        uint16_t pid = ((p[i + 1] & 0x1F) << 8) | p[i + 2];
        int es = ((p[i + 3] & 0x0F) << 8) | p[i + 4];
        h->pid_stream_type[pid] = ty;
        if (pr && pr->stream_count < MAX_STREAMS_PER_PROG) {
            pr->streams[pr->stream_count].pid = pid;
            pr->streams[pr->stream_count].stream_type = ty;
            pr->stream_count++;
        }
        if (!h->live->pid_is_referenced[pid]) tsa_reset_pid_stats(h, pid);
        h->live->pid_is_referenced[pid] = true;
        i += 5 + es;
    }
}

tsa_handle_t* tsa_create(const tsa_config_t* cfg) {
    tsa_handle_t* h = calloc(1, sizeof(tsa_handle_t));
    if (!h) return NULL;
    ALLOC_OR_GOTO(h->pid_status, tsa_measurement_status_t, TS_PID_MAX);
    ALLOC_OR_GOTO(h->pid_eb_fill_q64, int128_t, TS_PID_MAX);
    ALLOC_OR_GOTO(h->pid_tb_fill_q64, int128_t, TS_PID_MAX);
    ALLOC_OR_GOTO(h->pid_mb_fill_q64, int128_t, TS_PID_MAX);
    ALLOC_OR_GOTO(h->last_buffer_leak_vstc, uint64_t, TS_PID_MAX);
    ALLOC_OR_GOTO(h->pid_bitrate_ema, double, TS_PID_MAX);
    ALLOC_OR_GOTO(h->pid_bitrate_min, uint64_t, TS_PID_MAX);
    ALLOC_OR_GOTO(h->pid_bitrate_max, uint64_t, TS_PID_MAX);
    ALLOC_OR_GOTO(h->last_cc, uint8_t, TS_PID_MAX);
    ALLOC_OR_GOTO(h->ignore_next_cc, bool, TS_PID_MAX);
    ALLOC_OR_GOTO(h->pid_seen, bool, TS_PID_MAX);
    ALLOC_OR_GOTO(h->pid_is_pmt, bool, TS_PID_MAX);
    ALLOC_OR_GOTO(h->pid_stream_type, uint8_t, TS_PID_MAX);
    ALLOC_OR_GOTO(h->pid_to_active_idx, int16_t, TS_PID_MAX);
    ALLOC_OR_GOTO(h->pid_pes_buf, uint8_t*, TS_PID_MAX);
    ALLOC_OR_GOTO(h->pid_pes_len, uint32_t, TS_PID_MAX);
    ALLOC_OR_GOTO(h->pid_pes_cap, uint32_t, TS_PID_MAX);
    ALLOC_OR_GOTO(h->pid_width, uint16_t, TS_PID_MAX);
    ALLOC_OR_GOTO(h->pid_height, uint16_t, TS_PID_MAX);
    ALLOC_OR_GOTO(h->pid_profile, uint8_t, TS_PID_MAX);
    ALLOC_OR_GOTO(h->pid_audio_sample_rate, uint32_t, TS_PID_MAX);
    ALLOC_OR_GOTO(h->pid_audio_channels, uint8_t, TS_PID_MAX);
    ALLOC_OR_GOTO(h->pid_log2_max_frame_num, uint8_t, TS_PID_MAX);
    ALLOC_OR_GOTO(h->pid_last_frame_num, uint32_t, TS_PID_MAX);
    ALLOC_OR_GOTO(h->pid_frame_num_valid, bool, TS_PID_MAX);
    ALLOC_OR_GOTO(h->pid_gop_n, uint32_t, TS_PID_MAX);
    ALLOC_OR_GOTO(h->pid_last_gop_n, uint32_t, TS_PID_MAX);
    ALLOC_OR_GOTO(h->pid_gop_min, uint32_t, TS_PID_MAX);
    ALLOC_OR_GOTO(h->pid_gop_max, uint32_t, TS_PID_MAX);
    ALLOC_OR_GOTO(h->pid_last_idr_ns, uint64_t, TS_PID_MAX);
    ALLOC_OR_GOTO(h->pid_gop_ms, uint32_t, TS_PID_MAX);
    ALLOC_OR_GOTO(h->pid_i_frames, uint64_t, TS_PID_MAX);
    ALLOC_OR_GOTO(h->pid_p_frames, uint64_t, TS_PID_MAX);
    ALLOC_OR_GOTO(h->pid_b_frames, uint64_t, TS_PID_MAX);
    h->live = calloc(1, sizeof(tsa_tr101290_stats_t));
    h->prev_snap_base = calloc(1, sizeof(tsa_tr101290_stats_t));
    h->snap_state.stats = calloc(1, sizeof(tsa_snapshot_full_t));
    if (!h->live || !h->prev_snap_base || !h->snap_state.stats) goto fail;
    if (cfg) h->config = *cfg;
    if (h->config.pcr_ema_alpha <= 0) h->config.pcr_ema_alpha = 0.05;
    h->pcr_ema_alpha_q32 = TO_Q32_32(h->config.pcr_ema_alpha);
    ts_pcr_window_init(&h->pcr_window, 32);
    h->pool_size = 1024 * 1024 + 32 * 4096; /* 1MB + 32 slots * 4KB for PES */
    if (posix_memalign(&h->pool_base, 64, h->pool_size) != 0) h->pool_base = malloc(h->pool_size);
    h->pool_offset = 0;
    h->pes_total_allocated = 32 * 4096;
    h->pes_max_quota = 64 * 1024 * 1024;
    h->last_trigger_reason = -1;
    for (int i = 0; i < TS_PID_MAX; i++) {
        h->pid_to_active_idx[i] = -1;
        h->pid_gop_min[i] = 0xFFFFFFFF;
        h->pid_pes_buf[i] = NULL;
        h->pid_pes_cap[i] = 0;
    }
    /* Assign first 32 potential PES buffers to a pool */
    h->pes_pool_used = 0;
    return h;
fail:
    tsa_destroy(h);
    return NULL;
}

void tsa_destroy(tsa_handle_t* h) {
    if (!h) return;
    ts_pcr_window_destroy(&h->pcr_window);
    if (h->pool_base) free(h->pool_base);
    FREE_IF(h->pid_pes_buf);
    FREE_IF(h->pid_status);
    FREE_IF(h->pid_eb_fill_q64);
    FREE_IF(h->pid_tb_fill_q64);
    FREE_IF(h->pid_mb_fill_q64);
    FREE_IF(h->last_buffer_leak_vstc);
    FREE_IF(h->pid_bitrate_ema);
    FREE_IF(h->pid_bitrate_min);
    FREE_IF(h->pid_bitrate_max);
    FREE_IF(h->last_cc);
    FREE_IF(h->ignore_next_cc);
    FREE_IF(h->pid_seen);
    FREE_IF(h->pid_is_pmt);
    FREE_IF(h->pid_stream_type);
    FREE_IF(h->pid_to_active_idx);
    FREE_IF(h->pid_pes_len);
    FREE_IF(h->pid_pes_cap);
    FREE_IF(h->pid_width);
    FREE_IF(h->pid_height);
    FREE_IF(h->pid_profile);
    FREE_IF(h->pid_audio_sample_rate);
    FREE_IF(h->pid_audio_channels);
    FREE_IF(h->pid_log2_max_frame_num);
    FREE_IF(h->pid_last_frame_num);
    FREE_IF(h->pid_frame_num_valid);
    FREE_IF(h->pid_gop_n);
    FREE_IF(h->pid_last_gop_n);
    FREE_IF(h->pid_gop_min);
    FREE_IF(h->pid_gop_max);
    FREE_IF(h->pid_last_idr_ns);
    FREE_IF(h->pid_gop_ms);
    FREE_IF(h->pid_i_frames);
    FREE_IF(h->pid_p_frames);
    FREE_IF(h->pid_b_frames);
    FREE_IF(h->live);
    FREE_IF(h->prev_snap_base);
    FREE_IF(h->snap_state.stats);
    free(h);
}

static void tsa_reset_pid_stats(tsa_handle_t* h, uint16_t pid) {
    h->pid_seen[pid] = false;
    h->live->pid_packet_count[pid] = 0;
    h->live->pid_bitrate_bps[pid] = 0;
    h->live->pid_cc_errors[pid] = 0;
    h->pid_bitrate_min[pid] = 0;
    h->pid_bitrate_max[pid] = 0;
    h->ignore_next_cc[pid] = false;
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
    h->pid_eb_fill_q64[pid] = 0;
    h->pid_tb_fill_q64[pid] = 0;
    h->pid_mb_fill_q64[pid] = 0;
    h->live->pid_eb_fill_bytes[pid] = 0;
    h->live->pid_eb_fill_pct[pid] = 0;
    h->last_buffer_leak_vstc[pid] = 0;
    h->pid_status[pid] = TSA_STATUS_VALID;
}

static int16_t tsa_update_pid_tracker(tsa_handle_t* h, uint16_t p) {
    int16_t idx = h->pid_to_active_idx[p];
    if (idx == -1) {
        if (h->pid_tracker_count < MAX_ACTIVE_PIDS) {
            h->pid_active_list[h->pid_tracker_count] = p;
            h->pid_to_active_idx[p] = h->pid_tracker_count;
            idx = h->pid_tracker_count++;
        } else {
            int ev = -1;
            for (int i = 0; i < MAX_ACTIVE_PIDS; i++) {
                bool prot = false;
                uint16_t c = h->pid_active_list[i];
                if (c <= 1 || h->pid_is_pmt[c])
                    prot = true;
                else {
                    for (int j = 0; j < 16; j++)
                        if (h->config.protected_pids[j] == c) {
                            prot = true;
                            break;
                        }
                }
                if (!prot) {
                    ev = i;
                    break;
                }
            }
            if (ev == -1) ev = 0;
            uint16_t ep = h->pid_active_list[ev];
            h->pid_to_active_idx[ep] = -1;
            tsa_reset_pid_stats(h, ep);
            for (int i = ev; i < MAX_ACTIVE_PIDS - 1; i++) {
                h->pid_active_list[i] = h->pid_active_list[i + 1];
                h->pid_to_active_idx[h->pid_active_list[i]] = i;
            }
            h->pid_active_list[MAX_ACTIVE_PIDS - 1] = p;
            h->pid_to_active_idx[p] = MAX_ACTIVE_PIDS - 1;
            idx = MAX_ACTIVE_PIDS - 1;
        }
        h->pid_seen[p] = true;
    }
    if (idx != (int16_t)h->pid_tracker_count - 1) {
        uint16_t cur = h->pid_active_list[idx];
        for (uint32_t i = (uint32_t)idx; i < h->pid_tracker_count - 1; i++) {
            h->pid_active_list[i] = h->pid_active_list[i + 1];
            h->pid_to_active_idx[h->pid_active_list[i]] = i;
        }
        h->pid_active_list[h->pid_tracker_count - 1] = cur;
        h->pid_to_active_idx[cur] = h->pid_tracker_count - 1;
        idx = h->pid_tracker_count - 1;
    }
    return idx;
}

void tsa_decode_packet_pure(tsa_handle_t* h, const uint8_t* p, uint64_t n, ts_decode_result_t* r) {
    if (!h || !p || !r) return;
    (void)n;
    r->pid = ((p[1] & 0x1F) << 8) | p[2];
    r->pusi = (p[1] & 0x40);
    r->af_len = (p[3] & 0x20) ? p[4] + 1 : 0;
    r->payload_len = 188 - 4 - r->af_len;
    r->has_payload = (p[3] & 0x10) && r->payload_len > 0;
    r->cc = p[3] & 0x0F;
    r->has_discontinuity = (r->af_len > 1) && (p[5] & 0x80);
}
void tsa_decode_packet(tsa_handle_t* h, const uint8_t* p, uint64_t n, ts_decode_result_t* r) {
    if (!h || !p || !r) return;
    tsa_decode_packet_pure(h, p, n, r);
    h->live->total_ts_packets++;
    tsa_update_pid_tracker(h, r->pid);
    h->live->pid_packet_count[r->pid]++;
    if (r->pid == 0 && r->pusi) {
        h->last_pat_ns = n;
        process_pat(h, p, n);
    } else if (h->pid_is_pmt[r->pid] && r->pusi) {
        h->last_pmt_ns = n;
        process_pmt(h, r->pid, p, n);
    }
}

void tsa_handle_internal_drop(tsa_handle_t* h, uint64_t drop_count) {
    if (!h) return;
    h->live->internal_analyzer_drop += drop_count;
    for (int i = 0; i < TS_PID_MAX; i++) {
        if (h->pid_seen[i]) {
            h->ignore_next_cc[i] = true;
        }
    }
}

void tsa_metrology_process(tsa_handle_t* h, const uint8_t* pkt, uint64_t now, const ts_decode_result_t* res) {
    if (!h || !pkt || !res) return;
    uint16_t pid = res->pid;
    h->pkts_since_pcr++;

    // Track every single PID seen for metrics
    tsa_update_pid_tracker(h, pid);

    static uint64_t l_now = 0, off = 0;
    if (now == l_now) {
        uint64_t br = h->live->physical_bitrate_bps ? h->live->physical_bitrate_bps : 10000000;
        off += (1504ULL * 1000000000ULL) / br;
        now += off;
    } else {
        l_now = now;
        off = 0;
    }
    if (h->live->pid_last_seen_ns[pid] > 0) {
        uint64_t dt = h->stc_ns - h->live->pid_last_seen_ns[pid];
        int128_t l_eb = TO_Q64_64(0.04), l_tb = TO_Q64_64(0.05);
        uint8_t st = h->pid_stream_type[pid];
        if (st == 0x1b) {
            l_eb = TO_Q64_64(0.04);
            l_tb = TO_Q64_64(0.05);
        } else if (st == 0x24) {
            l_eb = TO_Q64_64(0.06);
            l_tb = TO_Q64_64(0.08);
        } else if (st == 0x03 || st == 0x04 || st == 0x0f || st == 0x11 || st == 0x81) {
            l_eb = TO_Q64_64(0.002);
            l_tb = TO_Q64_64(0.004);
        }
        h->pid_tb_fill_q64[pid] -= (l_tb * dt);
        if (h->pid_tb_fill_q64[pid] < 0) h->pid_tb_fill_q64[pid] = 0;
        h->pid_mb_fill_q64[pid] -= (l_tb * dt);
        if (h->pid_mb_fill_q64[pid] < 0) h->pid_mb_fill_q64[pid] = 0;
        if (h->live->pid_is_referenced[pid]) {
            h->pid_eb_fill_q64[pid] -= (l_eb * dt);
            if (h->pid_eb_fill_q64[pid] < 0) h->pid_eb_fill_q64[pid] = 0;
        }
    }
    h->pid_tb_fill_q64[pid] += INT_TO_Q64_64(188);
    if (res->has_payload) {
        h->pid_mb_fill_q64[pid] += INT_TO_Q64_64(res->payload_len);
        if (h->live->pid_is_referenced[pid]) h->pid_eb_fill_q64[pid] += INT_TO_Q64_64(res->payload_len);
    }
    if (res->pusi && h->live->pid_is_referenced[pid]) {
        h->pid_eb_fill_q64[pid] -= INT_TO_Q64_64(100000);
        if (h->pid_eb_fill_q64[pid] < 0) h->pid_eb_fill_q64[pid] = 0;
    }
    h->live->pid_eb_fill_bytes[pid] = (uint32_t)(h->pid_eb_fill_q64[pid] >> 64);
    h->live->pid_last_seen_ns[pid] = h->stc_ns;
    if (pkt[1] & 0x80) {
        h->live->transport_error.count++;
        h->live->transport_error.last_timestamp_ns = now;
    }
    if (h->live->pid_packet_count[pid] > 1 && !res->has_discontinuity) {
        if (h->ignore_next_cc[pid]) {
            h->ignore_next_cc[pid] = false;
        } else {
            ts_cc_status_t s =
                cc_classify_error(h->last_cc[pid], res->cc, res->has_payload, (pkt[3] & 0x20) && !(pkt[3] & 0x10));
            if (s == TS_CC_LOSS) {
                h->live->cc_error.count++;
                h->live->cc_error.last_timestamp_ns = now;
                h->live->cc_error.triggering_vstc = h->stc_ns;
                h->live->cc_error.absolute_byte_offset = h->live->total_ts_packets * 188;
                h->live->pid_cc_errors[pid]++;
                h->live->latched_cc_error = 1;
                h->pid_status[pid] = TSA_STATUS_DEGRADED;
                h->live->cc_loss_count += (res->cc - ((h->last_cc[pid] + 1) & 0x0F)) & 0x0F;
            } else if (s == TS_CC_DUPLICATE)
                h->live->cc_duplicate_count++;
            else if (s == TS_CC_OUT_OF_ORDER) {
                h->live->cc_error.count++;
                h->live->cc_error.last_timestamp_ns = now;
                h->pid_status[pid] = TSA_STATUS_DEGRADED;
            }
        }
    }
    h->last_cc[pid] = res->cc;
    if (res->payload_len > 0 && h->live->pid_is_referenced[pid]) {
        if (res->pusi) {
            if (h->pid_pes_len[pid] > 0)
                tsa_handle_es_payload(h, pid, h->pid_pes_buf[pid], h->pid_pes_len[pid], h->stc_ns);
            h->pid_pes_len[pid] = 0;

            /* If no buffer assigned yet, grab one from the pool */
            if (h->pid_pes_buf[pid] == NULL && h->pes_pool_used < 32) {
                h->pid_pes_buf[pid] = tsa_mem_pool_alloc(h, 4096);
                h->pid_pes_cap[pid] = 4096;
                h->pes_pool_used++;
            }
        }
        if (h->pid_pes_buf[pid] && h->pid_pes_len[pid] + res->payload_len <= h->pid_pes_cap[pid]) {
            memcpy(h->pid_pes_buf[pid] + h->pid_pes_len[pid], pkt + 4 + res->af_len, res->payload_len);
            h->pid_pes_len[pid] += res->payload_len;
        }
    }
    if ((pkt[3] & 0x20) && pkt[4] > 0 && (pkt[5] & 0x10)) {
        h->live->pid_is_referenced[pid] = true;
        tsa_update_pid_tracker(h, pid);
        uint64_t pt = extract_pcr(pkt), pn = pt * 1000 / 27;
        ts_pcr_window_add(&h->pcr_window, now, pn, 0);
        if (h->last_pcr_arrival_ns > 0) {
            uint64_t ds = now - h->last_pcr_arrival_ns;
            if (ds / 1000000ULL > 40) {
                h->live->pcr_repetition_error.count++;
                h->live->pcr_repetition_error.last_timestamp_ns = now;
            }
            if (ds / 1000000ULL > h->live->pcr_repetition_max_ms) h->live->pcr_repetition_max_ms = ds / 1000000ULL;
            int64_t dp = (int64_t)pt - (int64_t)h->last_pcr_ticks;
            if (dp < -((int64_t)1 << 41))
                dp += ((int64_t)1 << 42);
            else if (dp > ((int64_t)1 << 41))
                dp -= ((int64_t)1 << 42);
            int64_t ij = (int64_t)ds - dp * 1000 / 27;
            if ((uint64_t)abs((int)ij) > h->live->pcr_jitter_max_ns)
                h->live->pcr_jitter_max_ns = (uint64_t)abs((int)ij);
            int128_t sq;
            int64_t ra = 0;
            int rr = ts_pcr_window_regress(&h->pcr_window, &sq, NULL, &ra);
            h->live->pcr_accuracy_ns = (double)ra;
            if (dp > 0) {
                unsigned __int128 b128 = (unsigned __int128)h->pkts_since_pcr * 1504 * 27000000;
                uint64_t br = (uint64_t)(b128 / dp);
                uint64_t al = (uint64_t)h->pcr_ema_alpha_q32;
                if (h->live->pcr_bitrate_bps == 0)
                    h->live->pcr_bitrate_bps = br;
                else
                    h->live->pcr_bitrate_bps = ((unsigned __int128)br * al +
                                                (unsigned __int128)h->live->pcr_bitrate_bps * ((1ULL << 32) - al)) >>
                                               32;
            }
            if (rr == 0) {
                h->stc_locked = true;
                h->stc_slope_q64 = sq;
                if (ra > 500) {
                    h->live->pcr_accuracy_error.count++;
                    h->live->pcr_accuracy_error.last_timestamp_ns = now;
                }
                h->pcr_jitter_sq_sum_ns += (int128_t)ij * ij;
                h->pcr_jitter_count++;
                h->live->pcr_jitter_avg_ns = sqrt((double)h->pcr_jitter_sq_sum_ns / h->pcr_jitter_count);
            } else
                h->stc_locked = false;
        } else
            h->stc_ns = pn;
        h->last_pcr_ticks = pt;
        h->last_pcr_arrival_ns = now;
        h->pkts_since_pcr = 0;
    }
}

void tsa_process_packet(tsa_handle_t* h, const uint8_t* p, uint64_t n) {
    if (!h || !p) return;
    if (!h->engine_started) {
        h->start_ns = n;
        h->engine_started = true;
        h->last_snap_ns = n;
        h->last_pcr_arrival_ns = n;
        h->stc_ns = n;
        h->last_pat_ns = n;
        h->last_pmt_ns = n;
    }
    if (p[0] != 0x47) {
        h->consecutive_sync_errors++;
        h->consecutive_good_syncs = 0;
        if (h->consecutive_sync_errors >= 5 && h->signal_lock) {
            h->live->sync_loss.count++;
            h->live->sync_loss.last_timestamp_ns = n;
            h->signal_lock = false;
        }
        h->live->sync_byte_error.count++;
        h->live->sync_byte_error.last_timestamp_ns = n;
        return;
    } else {
        h->consecutive_good_syncs++;
        h->consecutive_sync_errors = 0;
        if (h->consecutive_good_syncs >= 2) h->signal_lock = true;
    }
    if (h->stc_locked)
        h->stc_ns = (uint64_t)((h->stc_intercept_q64 + (int128_t)n * h->stc_slope_q64) >> 64);
    else
        h->stc_ns = n;
    ts_decode_result_t r;
    tsa_decode_packet(h, p, n, &r);
    tsa_metrology_process(h, p, n, &r);
}

void tsa_commit_snapshot(tsa_handle_t* h, uint64_t n) {
    if (!h) return;
    if (n == 0) n = h->last_snap_ns;
    if (!h->stc_locked) h->stc_ns = n;
    uint64_t stc = h->stc_ns, dt = n - h->last_snap_ns;
    if (dt == 0) dt = 1;
    uint64_t dp = h->live->total_ts_packets - h->prev_snap_base->total_ts_packets;
    if (dp > 0) h->live->physical_bitrate_bps = (uint64_t)(((unsigned __int128)dp * 1504 * 1000000000ULL) / dt);
    h->live->mdi_mlr_pkts_s =
        (double)((h->live->cc_loss_count - h->prev_snap_base->cc_loss_count) * 1000000000ULL) / dt;
    h->live->mdi_df_ms = (double)h->live->pcr_jitter_max_ns / 1000000.0;
    h->live->stream_utc_ms = stc / 1000000ULL;
    if (h->live->total_ts_packets > 0) {
        if (n - h->last_pat_ns > 500000000ULL) {
            h->live->pat_error.count++;
            h->live->pat_error.last_timestamp_ns = n;
        }
        if (h->program_count > 0 && n - h->last_pmt_ns > 500000000ULL) {
            h->live->pmt_error.count++;
            h->live->pmt_error.last_timestamp_ns = n;
        }
    }
    int128_t sq;
    int64_t pa = 0;
    double sl = 1.0;
    if (ts_pcr_window_regress(&h->pcr_window, &sq, NULL, &pa) == 0) {
        sl = (double)sq / (double)((int128_t)1 << 64);
        h->live->pcr_drift_ppm = (sl - 1.0) * 1000000.0;
        h->live->pcr_accuracy_ns = (double)pa;
    }
    float rn = 999.0f, re = 999.0f;
    if (h->live->pcr_bitrate_bps > h->live->physical_bitrate_bps && h->live->physical_bitrate_bps > 0) {
        uint32_t la = h->srt_live.effective_rcv_latency_ms ? h->srt_live.effective_rcv_latency_ms : 50,
                 ji = (uint32_t)(h->live->pcr_jitter_max_ns / 1000000ULL);
        if (la > ji)
            rn = (float)((uint64_t)(la - ji) * h->live->pcr_bitrate_bps / 1000) /
                 (float)(h->live->pcr_bitrate_bps - h->live->physical_bitrate_bps);
        else
            rn = 0.0f;
    }
    double dr = fabs(sl - 1.0);
    if (dr > 0.000001) re = (float)((100.0 - h->live->pcr_accuracy_ns / 1000000.0) / dr / 1000.0);
    double cn = 0, ce = 0;
    if (h->live->mdi_mlr_pkts_s > 0) cn += 0.8;
    if (rn < 5.0) cn += (5.0 - rn) / 5.0;
    if (h->live->pcr_jitter_max_ns > 10000000ULL)
        ce += 0.8;
    else if (h->live->pcr_jitter_max_ns > 500000ULL)
        ce += 0.5;
    if (h->live->pcr_accuracy_error.count > h->prev_snap_base->pcr_accuracy_error.count) ce += 0.4;
    for (int p = 0; p < TS_PID_MAX; p++)
        if (h->pid_seen[p] && h->pid_eb_fill_q64[p] == 0 && h->live->pid_is_referenced[p]) ce += 0.9;
    h->snap_state.stats->predictive.fault_domain =
        (cn > 0.6 && ce < 0.2) ? 1 : (ce > 0.6 && cn < 0.2) ? 2 : (cn > 0.4 && ce > 0.4) ? 3 : 0;
    // Signal Loss Check (Timeout: 500ms relative to system time n)
    bool any_packets_recently = false;
    for (int p = 0; p < TS_PID_MAX; p++) {
        if (h->pid_seen[p]) {
            if (h->live->pid_last_seen_ns[p] > 0 && n > h->live->pid_last_seen_ns[p]) {
                uint64_t dt = n - h->live->pid_last_seen_ns[p];
                if (dt < 500000000ULL) {
                    any_packets_recently = true;
                    break;
                }
            }
        }
    }
    if (!any_packets_recently && h->live->total_ts_packets > 0) {
        h->signal_lock = false;
    }

    // BASE SCORING
    float current_health = 100.0f;
    if (!h->signal_lock) {
        current_health = 0.0f;
    } else {
        // Penalty for SRT/Network Jitter
        if (rn < 5.0f) {
            current_health -= (5.0f - rn) * 4.0f;
        }
        // Penalty for Encoder Drift
        if (re < 30.0f) {
            current_health -= (30.0f - re) * 0.5f;
        }
        // AGGRESSIVE Penalty for CC Errors
        uint64_t total_cc = h->live->cc_error.count;
        uint64_t prev_cc = h->prev_snap_base->cc_error.count;
        if (total_cc > prev_cc) {
            current_health -= 25.0f;  // Drop 25 points immediately on CC error
        }
    }

    if (current_health < 0) current_health = 0;

    // Smooth Transition (to make errors visible on dashboard)
    // If the new score is lower, jump there immediately.
    // If higher, recover slowly.
    if (current_health < h->last_health_score || h->last_health_score < 0.1) {
        h->last_health_score = current_health;
    } else {
        h->last_health_score = h->last_health_score * 0.8f + current_health * 0.2f;
    }
    float he = h->last_health_score;

    uint32_t s = atomic_load(&h->snap_state.seq);
    atomic_store(&h->snap_state.seq, s + 1);
    h->snap_state.stats->predictive.master_health = he;
    h->snap_state.stats->summary.master_health = he;
    h->snap_state.stats->summary.total_packets = h->live->total_ts_packets;
    h->snap_state.stats->summary.signal_lock = h->signal_lock;
    h->snap_state.stats->summary.physical_bitrate_bps = h->live->physical_bitrate_bps;
    h->snap_state.stats->stats = *h->live;
    uint32_t ai = 0;
    for (uint32_t i = 0; i < h->pid_tracker_count && ai < MAX_ACTIVE_PIDS; i++) {
        uint16_t p = h->pid_active_list[i];
        uint64_t pd = h->live->pid_packet_count[p] - h->prev_snap_base->pid_packet_count[p];
        if (pd > 0) {
            uint64_t cb = (pd * 1504 * 1000000000ULL) / dt;
            if (h->live->pid_bitrate_bps[p] == 0)
                h->live->pid_bitrate_bps[p] = cb;
            else
                h->live->pid_bitrate_bps[p] =
                    (cb * (uint64_t)h->pcr_ema_alpha_q32 +
                     h->live->pid_bitrate_bps[p] * ((1ULL << 32) - (uint64_t)h->pcr_ema_alpha_q32)) >>
                    32;
            if (cb > 0) {
                if (h->pid_bitrate_min[p] == 0 || cb < h->pid_bitrate_min[p]) h->pid_bitrate_min[p] = cb;
                if (cb > h->pid_bitrate_max[p]) h->pid_bitrate_max[p] = cb;
            }
        }
        tsa_snapshot_full_t* sn = h->snap_state.stats;
        sn->pids[ai].pid = p;
        strncpy(sn->pids[ai].type_str, tsa_get_pid_type_name(h, p), 15);
        sn->pids[ai].bitrate_q16_16 = (int64_t)h->live->pid_bitrate_bps[p] << 16;
        sn->pids[ai].status = (uint8_t)h->pid_status[p];
        sn->pids[ai].width = h->pid_width[p];
        sn->pids[ai].height = h->pid_height[p];
        sn->pids[ai].i_frames = h->pid_i_frames[p];
        sn->pids[ai].eb_fill_pct = (float)((double)(h->pid_eb_fill_q64[p] >> 64) * 100.0 / 1200000.0);
        ai++;
    }
    h->snap_state.stats->active_pid_count = ai;
    atomic_store(&h->snap_state.seq, s + 2);
    *h->prev_snap_base = *h->live;
    h->last_snap_ns = n;
    h->live->pcr_jitter_max_ns = 0;
}

const char* tsa_get_pid_type_name(const tsa_handle_t* h, uint16_t p) {
    if (p == 0) return "PAT";
    if (p == 0x1FFF) return "Stuffing";
    if (h->pid_is_pmt[p]) return "PMT";
    uint8_t ty = h->pid_stream_type[p];
    if (ty == 0) {
        for (uint32_t i = 0; i < h->program_count; i++) {
            for (uint32_t j = 0; j < h->programs[i].stream_count; j++)
                if (h->programs[i].streams[j].pid == p) {
                    ty = h->programs[i].streams[j].stream_type;
                    break;
                }
            if (ty != 0) break;
        }
    }
    return (ty != 0) ? tsa_stream_type_to_str(ty) : "Unknown";
}

ts_cc_status_t cc_classify_error(uint8_t l, uint8_t c, bool p, bool a) {
    if (!p || a) return TS_CC_OK;
    if (c == l) return TS_CC_DUPLICATE;
    if (c == ((l + 1) & 0xF)) return TS_CC_OK;
    return ((c - l) & 0xF) < 8 ? TS_CC_LOSS : TS_CC_OUT_OF_ORDER;
}
uint32_t mpegts_crc32(const uint8_t* d, int l) {
    static const uint32_t t[256] = {
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
    uint32_t c = 0xFFFFFFFF;
    for (int i = 0; i < l; i++) c = (c << 8) ^ t[((c >> 24) ^ d[i]) & 0xFF];
    return c;
}
uint64_t extract_pcr(const uint8_t* p) {
    if (!(p[3] & 0x20) || p[4] == 0 || !(p[5] & 0x10)) return INVALID_PCR;
    uint64_t b =
        ((uint64_t)p[6] << 25) | ((uint64_t)p[7] << 17) | ((uint64_t)p[8] << 9) | ((uint64_t)p[9] << 1) | (p[10] >> 7);
    return b * 300 + (((uint16_t)(p[10] & 0x01) << 8) | p[11]);
}
void ts_pcr_window_init(ts_pcr_window_t* w, uint32_t s) {
    w->samples = calloc(s, sizeof(ts_pcr_sample_t));
    w->size = s;
    w->count = 0;
    w->head = 0;
}
void ts_pcr_window_destroy(ts_pcr_window_t* w) {
    if (w->samples) free(w->samples);
}
void ts_pcr_window_add(ts_pcr_window_t* w, uint64_t s, uint64_t p, uint64_t o) {
    (void)o;
    w->samples[w->head].sys_ns = s;
    w->samples[w->head].pcr_ns = p;
    w->head = (w->head + 1) % w->size;
    if (w->count < w->size) w->count++;
}
int ts_pcr_window_regress(ts_pcr_window_t* w, int128_t* s, int128_t* i, int64_t* r) {
    if (w->count < 2) return -1;
    double sx = 0, sy = 0, sxy = 0, sxx = 0;
    uint32_t n = w->count;
    uint64_t sts = w->samples[(w->head - n + w->size) % w->size].sys_ns,
             stp = w->samples[(w->head - n + w->size) % w->size].pcr_ns;
    for (uint32_t k = 0; k < n; k++) {
        uint32_t idx = (w->head - n + k + w->size) % w->size;
        double x = (double)(w->samples[idx].sys_ns - sts), y = (double)(w->samples[idx].pcr_ns - stp);
        sx += x;
        sy += y;
        sxy += x * y;
        sxx += x * x;
    }
    double det = (n * sxx - sx * sx);
    if (fabs(det) < 1e-9) return -1;
    double b = (n * sxy - sx * sy) / det, a = (sy - b * sx) / n;
    double me = 0;
    for (uint32_t k = 0; k < n; k++) {
        uint32_t idx = (w->head - n + k + w->size) % w->size;
        double err = fabs((double)(w->samples[idx].pcr_ns - stp) - (a + b * (double)(w->samples[idx].sys_ns - sts)));
        if (err > me) me = err;
    }
    if (r) *r = (int64_t)me;
    if (me > 10000000) return -1;
    if (s) *s = (int128_t)(b * (double)((int128_t)1 << 64));
    if (i) *i = (int128_t)(stp - b * sts + a);
    return 0;
}

int tsa_take_snapshot_lite(tsa_handle_t* h, tsa_snapshot_lite_t* s) {
    if (!h || !s) return -1;
    s->total_packets = h->live->total_ts_packets;
    s->physical_bitrate_bps = h->live->physical_bitrate_bps;
    s->active_pid_count = h->pid_tracker_count;
    s->signal_lock = h->signal_lock;
    s->master_health = h->snap_state.stats->summary.master_health;
    return 0;
}
int tsa_take_snapshot_full(tsa_handle_t* h, tsa_snapshot_full_t* s) {
    if (!h || !s) return -1;
    uint32_t s1, s2;
    do {
        s1 = atomic_load(&h->snap_state.seq);
        *s = *h->snap_state.stats;
        s2 = atomic_load(&h->snap_state.seq);
    } while (s1 != s2 || (s1 & 1));
    return 0;
}
void tsa_update_srt_stats(tsa_handle_t* h, const tsa_srt_stats_t* s) {
    if (!h || !s) return;
    h->srt_live = *s;
}
void* tsa_mem_pool_alloc(tsa_handle_t* h, size_t sz) {
    if (!h || !h->pool_base) return NULL;
    size_t al = (h->pool_offset + 63) & ~63;
    if (al + sz > h->pool_size) return NULL;
    void* p = (uint8_t*)h->pool_base + al;
    h->pool_offset = al + ((sz > 64) ? sz : 64);
    return p;
}
size_t tsa_snapshot_to_json(const tsa_snapshot_full_t* sn, char* b, size_t s) {
    if (!sn || !b || s < 1024) return 0;
    int off = 0;
    int n = snprintf(b + off, s - off,
                     "{\"status\":\"ok\",\"master_health\":%.1f,\"metrics\":{\"bitrate_bps\":%llu},\"pids\":[",
                     sn->summary.master_health, (unsigned long long)sn->stats.physical_bitrate_bps);
    if (n > 0) off += n;
    for (uint32_t i = 0; i < sn->active_pid_count; i++) {
        n = snprintf(b + off, s - off, "%s{\"pid\":\"0x%04x\",\"type\":\"%s\",\"bps\":%llu}", (i == 0) ? "" : ",",
                     sn->pids[i].pid, sn->pids[i].type_str, (unsigned long long)(sn->pids[i].bitrate_q16_16 >> 16));
        if (n > 0) off += n;
    }
    n = snprintf(b + off, s - off, "]}");
    if (n > 0) off += n;
    return (size_t)off;
}
float tsa_get_pid_tb_fill(tsa_handle_t* h, uint16_t p) {
    return (float)(h->pid_tb_fill_q64[p] >> 64);
}
float tsa_get_pid_mb_fill(tsa_handle_t* h, uint16_t p) {
    return (float)(h->pid_mb_fill_q64[p] >> 64);
}
float tsa_get_pid_eb_fill(tsa_handle_t* h, uint16_t p) {
    return (float)(h->pid_eb_fill_q64[p] >> 64);
}

void tsa_reset_latched_errors(tsa_handle_t* h) {
    if (!h || !h->live) return;
    h->live->latched_cc_error = 0;
}
void tsa_forensic_generate_json(tsa_handle_t* h, char* b, size_t s) {
    if (!h || !b || s < 256) return;
    snprintf(b, s, "{\"event\":\"forensic_capture\",\"trigger_reason\":%d,\"total_packets\":%llu}",
             h->last_trigger_reason, (unsigned long long)h->live->total_ts_packets);
}

struct tsa_packet_ring {
    uint8_t* buffer;
    uint64_t* timestamps;
    size_t sz;
    _Atomic uint64_t head;
    _Atomic uint64_t tail;
};
tsa_packet_ring_t* tsa_packet_ring_create(size_t sz) {
    tsa_packet_ring_t* r = calloc(1, sizeof(struct tsa_packet_ring));
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
    if (r->buffer) free(r->buffer);
    if (r->timestamps) free(r->timestamps);
    free(r);
}
int tsa_packet_ring_push(tsa_packet_ring_t* r, const uint8_t* p, uint64_t n) {
    if (!r || !p) return -1;
    uint64_t h = atomic_load_explicit(&r->head, memory_order_relaxed),
             t = atomic_load_explicit(&r->tail, memory_order_acquire);
    if (h - t >= r->sz) return -1;
    size_t idx = h % r->sz;
    memcpy(r->buffer + idx * 188, p, 188);
    r->timestamps[idx] = n;
    atomic_store_explicit(&r->head, h + 1, memory_order_release);
    return 0;
}
int tsa_packet_ring_pop(tsa_packet_ring_t* r, uint8_t* p, uint64_t* n) {
    if (!r || !p) return -1;
    uint64_t t = atomic_load_explicit(&r->tail, memory_order_relaxed),
             h = atomic_load_explicit(&r->head, memory_order_acquire);
    if (h == t) return -1;
    size_t idx = t % r->sz;
    memcpy(p, r->buffer + idx * 188, 188);
    if (n) *n = r->timestamps[idx];
    atomic_store_explicit(&r->tail, t + 1, memory_order_release);
    return 0;
}
bool tsa_forensic_trigger(tsa_handle_t* h, int r) {
    if (!h) return false;
    uint64_t c = h->live->sync_loss.count + h->live->pat_error.count + h->live->cc_error.count +
                 h->live->pmt_error.count + h->live->pid_error.count + h->live->crc_error.count;
    if (h->last_trigger_reason == -1 || h->last_trigger_reason != r) {
        h->last_trigger_reason = r;
        h->last_forensic_alarm_count = c;
        return true;
    }
    if (c > h->last_forensic_alarm_count + 5) {
        h->last_forensic_alarm_count = c;
        return true;
    }
    return false;
}
struct tsa_forensic_writer {
    tsa_packet_ring_t* ring;
    FILE* fp;
    pthread_t thread;
    _Atomic bool running;
    char filename[256];
};
static void* forensic_writer_thread(void* arg) {
    tsa_forensic_writer_t* w = (tsa_forensic_writer_t*)arg;
    uint8_t pkt[188];
    uint64_t ts;
    while (atomic_load(&w->running)) {
        if (tsa_packet_ring_pop(w->ring, pkt, &ts) == 0) {
            if (fwrite(pkt, 1, 188, w->fp) != 188) break;
        } else {
            usleep(10000);
        }
    }
    return NULL;
}
tsa_forensic_writer_t* tsa_forensic_writer_create(tsa_packet_ring_t* r, const char* f) {
    if (!r || !f) return NULL;
    tsa_forensic_writer_t* w = calloc(1, sizeof(struct tsa_forensic_writer));
    if (!w) return NULL;
    w->ring = r;
    strncpy(w->filename, f, 255);
    w->fp = fopen(f, "wb");
    if (!w->fp) {
        free(w);
        return NULL;
    }
    return w;
}
void tsa_forensic_writer_destroy(tsa_forensic_writer_t* w) {
    if (!w) return;
    atomic_store(&w->running, false);
    if (w->thread) pthread_join(w->thread, NULL);
    if (w->fp) fclose(w->fp);
    free(w);
}
void tsa_forensic_writer_start(tsa_forensic_writer_t* w) {
    if (!w || atomic_load(&w->running)) return;
    atomic_store(&w->running, true);
    pthread_create(&w->thread, NULL, forensic_writer_thread, w);
}
void tsa_forensic_writer_stop(tsa_forensic_writer_t* w) {
    if (!w || !atomic_load(&w->running)) return;
    atomic_store(&w->running, false);
    pthread_join(w->thread, NULL);
    w->thread = 0;
}
int tsa_forensic_writer_write_all(tsa_forensic_writer_t* w) {
    if (!w || !w->fp || !w->ring) return -1;
    uint8_t p[188];
    uint64_t n;
    int c = 0;
    while (tsa_packet_ring_pop(w->ring, p, &n) == 0) {
        if (fwrite(p, 1, 188, w->fp) != 188) break;
        c++;
    }
    fflush(w->fp);
    return c;
}

double calculate_shannon_entropy(const uint32_t* counts, int len) {
    if (!counts || len <= 0) return 0;
    uint64_t total = 0;
    for (int i = 0; i < len; i++) total += counts[i];
    if (total == 0) return 0;
    double e = 0;
    for (int i = 0; i < len; i++) {
        if (counts[i] > 0) {
            double p = (double)counts[i] / total;
            e -= p * log2(p);
        }
    }
    return e;
}

double calculate_pcr_jitter(uint64_t pcr, uint64_t now, double* drift) {
    (void)pcr;
    (void)now;
    if (drift) *drift = 0;
    return 0;
}

void tsa_render_dashboard(tsa_handle_t* h) {
    if (!h) return;
    tsa_snapshot_full_t snap;
    tsa_take_snapshot_full(h, &snap);
    printf("NOC Dashboard: Health %.1f%%, Bitrate %llu bps\n", snap.summary.master_health,
           (unsigned long long)snap.summary.physical_bitrate_bps);
}
