#define _GNU_SOURCE
#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "tsa_internal.h"

int128_t ts_now_ns128(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int128_t)ts.tv_sec * NS_PER_SEC + ts.tv_nsec;
}
#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* --- 1. String & Metric Formatting --- */

int tsa_fast_itoa(char* buf, int64_t val) {
    if (val == 0) {
        buf[0] = '0';
        buf[1] = '\0';
        return 1;
    }
    int i = 0, sign = (val < 0);
    uint64_t v = sign ? (uint64_t) - val : (uint64_t)val;
    while (v > 0) {
        buf[i++] = (v % 10) + '0';
        v /= 10;
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
    if (!b) return;
    b->base = buf;
    b->size = sz;
    b->offset = 0;
    if (sz > 0) b->base[0] = '\0';
}

void tsa_mbuf_append_str(tsa_metric_buffer_t* b, const char* s) {
    if (!b || !s) return;
    size_t len = strlen(s);
    size_t rem = (b->offset < b->size) ? (b->size - b->offset) : 0;
    if (rem <= 1) return;
    size_t cp = (len < rem - 1) ? len : rem - 1;
    memcpy(b->base + b->offset, s, cp);
    b->offset += cp;
    b->base[b->offset] = '\0';
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
    if (b->offset < b->size - 1) {
        b->base[b->offset++] = c;
        b->base[b->offset] = '\0';
    }
}

/* --- 2. MPEG-TS Protocol Helpers --- */

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

uint32_t tsa_crc32_check(const uint8_t* data, int len) { return mpegts_crc32(data, len); }

int tsa_parse_pes_header(const uint8_t* p, int len, tsa_pes_header_t* h) {
    if (len < 6 || p[0] != 0 || p[1] != 0 || p[2] != 1) return -1;
    h->stream_id = p[3];
    if (h->stream_id == 0xBC || h->stream_id == 0xBE || h->stream_id == 0xBF || h->stream_id == 0xF0 ||
        h->stream_id == 0xF1 || h->stream_id == 0xFF || h->stream_id == 0xF2 || h->stream_id == 0xF8) {
        h->has_pts = h->has_dts = false;
        h->header_len = 6;
        return 0;
    }
    if (len < 9) return -1;
    h->has_pts = (p[7] & 0x80);
    h->has_dts = (p[7] & 0x40);
    h->header_len = 9 + p[8];
    if (h->has_pts && len >= 14) {
        h->pts = ((uint64_t)(p[9] & 0x0E) << 29) | ((uint64_t)p[10] << 22) | ((uint64_t)(p[11] & 0xFE) << 14) |
                 ((uint64_t)p[12] << 7) | ((uint64_t)p[13] >> 1);
        h->dts = h->pts;
        if (h->has_dts && len >= 19) {
            h->dts = ((uint64_t)(p[14] & 0x0E) << 29) | ((uint64_t)p[15] << 22) | ((uint64_t)(p[16] & 0xFE) << 14) |
                     ((uint64_t)p[17] << 7) | ((uint64_t)p[18] >> 1);
        }
    }
    return 0;
}

ts_cc_status_t cc_classify_error(uint8_t l, uint8_t c, bool p, bool a) {
    if (!p || a) return TS_CC_OK;
    if (c == l) return TS_CC_DUPLICATE;
    if (c == ((l + 1) & 0xF)) return TS_CC_OK;
    return ((c - l) & 0xF) < 8 ? TS_CC_LOSS : TS_CC_OUT_OF_ORDER;
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
        case 0x56:
            return "Teletext";
        case 0x59:
            return "Subtitle";
        case 0x0f:
            return "ADTS-AAC";
        case 0x11:
            return "AAC-LATM";
        case 0x86:
            return "SCTE-35";
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

const char* tsa_get_pid_type_name(const tsa_handle_t* h, uint16_t p) {
    if (!h || p >= TS_PID_MAX) return "Unknown";
    if (p == 0) return "PAT";
    if (p == 0x1FFF) return "Stuffing";
    if (h->pid_is_pmt[p]) return "PMT";
    uint8_t ty = h->pid_stream_type[p];
    if (ty == 0) {
        for (uint32_t i = 0; i < h->program_count; i++)
            for (uint32_t j = 0; j < h->programs[i].stream_count; j++)
                if (h->programs[i].streams[j].pid == p) {
                    ty = h->programs[i].streams[j].stream_type;
                    break;
                }
    }
    return (ty != 0) ? tsa_stream_type_to_str(ty) : "Unknown";
}

/* --- 3. PCR Window Management --- */

void ts_pcr_window_init(ts_pcr_window_t* w, uint32_t s) {
    w->samples = calloc(s, sizeof(tsa_pcr_sample_t));
    w->size = s;
    w->count = 0;
    w->head = 0;
}
void ts_pcr_window_destroy(ts_pcr_window_t* w) {
    if (w->samples) free(w->samples);
    w->samples = NULL;
    w->size = 0;
    w->count = 0;
    w->head = 0;
}
void ts_pcr_window_add(ts_pcr_window_t* w, uint64_t s, uint64_t p, uint64_t o) {
    (void)o;
    w->samples[w->head].sys_ns = s;
    w->samples[w->head].pcr_ns = p;
    w->head = (w->head + 1) % w->size;
    if (w->count < w->size) w->count++;
}

/* --- 4. Professional Metrology --- */

double calculate_pcr_jitter(uint64_t pcr, uint64_t now, double* drift) {
    (void)pcr;
    (void)now;
    if (drift) *drift = 0;
    return 0;
}

double calculate_shannon_entropy(const uint32_t* counts, int len) {
    if (!counts || len <= 0) return 0;
    uint64_t total = 0;
    for (int i = 0; i < len; i++) total += counts[i];
    if (total == 0) return 0;
    double e = 0;
    for (int i = 0; i < len; i++)
        if (counts[i] > 0) {
            double p = (double)counts[i] / total;
            e -= p * log2(p);
        }
    return e;
}

/* --- 5. Forensic & Ring Buffer --- */

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
    free(r->buffer);
    free(r->timestamps);
    free(r);
}

int tsa_packet_ring_push(tsa_packet_ring_t* r, const uint8_t* p, uint64_t n) {
    if (!r || !p) return -1;
    uint64_t h = atomic_load_explicit(&r->head, memory_order_relaxed);
    uint64_t t = atomic_load_explicit(&r->tail, memory_order_acquire);
    if (h - t >= r->sz) return -1;
    size_t idx = h % r->sz;
    memcpy(r->buffer + idx * 188, p, 188);
    r->timestamps[idx] = n;
    atomic_store_explicit(&r->head, h + 1, memory_order_release);
    return 0;
}

int tsa_packet_ring_pop(tsa_packet_ring_t* r, uint8_t* p, uint64_t* n) {
    if (!r || !p) return -1;
    uint64_t t = atomic_load_explicit(&r->tail, memory_order_relaxed);
    uint64_t h = atomic_load_explicit(&r->head, memory_order_acquire);
    if (h == t) return -1;
    size_t idx = t % r->sz;
    memcpy(p, r->buffer + idx * 188, 188);
    if (n) *n = r->timestamps[idx];
    atomic_store_explicit(&r->tail, t + 1, memory_order_release);
    return 0;
}

bool tsa_forensic_trigger(tsa_handle_t* h, int reason) {
    if (!h) return false;
    uint64_t c = h->live->sync_loss.count + h->live->pat_error.count + h->live->cc_error.count +
                 h->live->pmt_error.count + h->live->pid_error.count + h->live->crc_error.count;
    if (h->last_trigger_reason == -1 || h->last_trigger_reason != reason) {
        h->last_trigger_reason = reason;
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
    tsa_metric_buffer_t* mbuf;  // Not used but reserved
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
        } else
            usleep(10000);
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

void tsa_forensic_generate_json(tsa_handle_t* h, char* b, size_t s) {
    if (h && b && s >= 256)
        snprintf(b, s, "{\"event\":\"forensic_capture\",\"total_packets\":%llu}",
                 (unsigned long long)h->live->total_ts_packets);
}

/* --- 6. Time Utilities --- */

int128_t ts_time_to_ns128(struct timespec ts) { return (int128_t)ts.tv_sec * NS_PER_SEC + ts.tv_nsec; }
struct timespec ns128_to_timespec(int128_t ns) {
    struct timespec ts;
    ts.tv_sec = (time_t)(ns / NS_PER_SEC);
    ts.tv_nsec = (long)(ns % NS_PER_SEC);
    return ts;
}

/* Extended SRT URL Parser: srt://host:port?mode=caller&latency=200&passphrase=abc&pbkeylen=16 */
int parse_url_ext(const char* url, char* host, int* port, int* is_listener, int* latency, char* passphrase,
                  int* pbkeylen) {
    if (strncmp(url, "srt://", 6) != 0) return -1;
    char buf[256];
    strncpy(buf, url + 6, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    char* query = strchr(buf, '?');
    if (query) *query++ = '\0';
    char* colon = strchr(buf, ':');
    if (!colon) return -1;
    *colon++ = '\0';
    strcpy(host, buf);
    *port = atoi(colon);
    if (is_listener) *is_listener = (strlen(host) == 0 || strcmp(host, "0.0.0.0") == 0);
    if (latency) *latency = 120;
    if (passphrase) passphrase[0] = '\0';
    if (pbkeylen) *pbkeylen = 0;
    if (query) {
        char* q_copy = strdup(query);
        char* token = strtok(q_copy, "&");
        while (token) {
            if (strncmp(token, "mode=", 5) == 0 && is_listener) {
                if (strcmp(token + 5, "listener") == 0)
                    *is_listener = 1;
                else if (strcmp(token + 5, "caller") == 0)
                    *is_listener = 0;
            } else if (strncmp(token, "latency=", 8) == 0 && latency) {
                *latency = atoi(token + 8);
            } else if (strncmp(token, "passphrase=", 11) == 0 && passphrase) {
                strncpy(passphrase, token + 11, 127);
                passphrase[127] = '\0';
            } else if (strncmp(token, "pbkeylen=", 9) == 0 && pbkeylen) {
                *pbkeylen = atoi(token + 9);
            }
            token = strtok(NULL, "&");
        }
        free(q_copy);
    }
    return 0;
}
