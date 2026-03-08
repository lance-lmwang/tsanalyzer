#define _GNU_SOURCE
#include "tsp.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <pthread.h>
#include <sched.h>
#include <srt.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include "tsa_log.h"
#define TAG "TSP"

/* SPSC Ring Mock Structure for Internal Consistency */
struct spsc_ring {
    uint8_t* buffer;
    size_t sz;
    size_t elem_sz;
    _Atomic uint64_t head;
    _Atomic uint64_t tail;
};

static uint64_t get_now_ns() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + ts.tv_nsec;
}

static void pacer_sync_wallclock(tsp_handle_t* h, uint64_t pcr, uint64_t now_ns) {
    h->base_pcr_ticks = pcr;
    /* Industrial Grade: 100ms startup buffer to absorb early jitter */
    h->base_wall_ns = now_ns + 100000000ULL;
    h->last_scheduled_ns = h->base_wall_ns;
    h->last_pcr_reset_time = time(NULL);
}

static int setup_srt(tsp_handle_t* h, const char* url) {
    tsa_info(TAG, "Setting up SRT: %s", url);
    char host[256];
    int port, is_l, lat, pb;
    char pass[128] = "";
    if (parse_url_ext(url, host, &port, &is_l, &lat, pass, &pb) != 0) return -1;

    srt_startup();
    h->srt_sock = srt_create_socket();
    int transtype = SRTT_LIVE, buf_size = 2000000, tlpktdrop = 1, tsbpd = 0, sync = 1;
    if (pass[0]) {
        srt_setsockopt(h->srt_sock, 0, SRTO_PASSPHRASE, pass, strlen(pass));
        srt_setsockopt(h->srt_sock, 0, SRTO_PBKEYLEN, &pb, sizeof(pb));
    }
    srt_setsockopt(h->srt_sock, 0, SRTO_TRANSTYPE, &transtype, sizeof(transtype));
    srt_setsockopt(h->srt_sock, 0, SRTO_LATENCY, &lat, sizeof(lat));
    srt_setsockopt(h->srt_sock, 0, SRTO_SNDBUF, &buf_size, sizeof(buf_size));
    srt_setsockopt(h->srt_sock, 0, SRTO_RCVBUF, &buf_size, sizeof(buf_size));
    srt_setsockopt(h->srt_sock, 0, SRTO_TSBPDMODE, &tsbpd, sizeof(tsbpd));
    srt_setsockopt(h->srt_sock, 0, SRTO_TLPKTDROP, &tlpktdrop, sizeof(tlpktdrop));
    srt_setsockopt(h->srt_sock, 0, SRTO_RCVSYN, &sync, sizeof(sync));
    srt_setsockopt(h->srt_sock, 0, SRTO_SNDSYN, &sync, sizeof(sync));

    struct sockaddr_in sa = {0};
    sa.sin_family = AF_INET;
    sa.sin_port = htons(port);
    inet_pton(AF_INET, (host[0] == '\0' || strcmp(host, "0.0.0.0") == 0) ? "0.0.0.0" : host, &sa.sin_addr.s_addr);

    if (is_l) {
        if (srt_bind(h->srt_sock, (struct sockaddr*)&sa, sizeof(sa)) == SRT_ERROR) return -1;
        srt_listen(h->srt_sock, 1);
        SRTSOCKET c = srt_accept(h->srt_sock, NULL, NULL);
        if (c == SRT_INVALID_SOCK) return -1;
        srt_close(h->srt_sock);
        h->srt_sock = c;
    } else {
        if (srt_connect(h->srt_sock, (struct sockaddr*)&sa, sizeof(sa)) == SRT_ERROR) return -1;
    }
    h->srt_enabled = true;
    return 0;
}

static void* tx_loop(void* arg) {
    tsp_handle_t* h = (tsp_handle_t*)arg;
    tsa_info(TAG, "Professional Pacer Active");
    if (h->cfg.cpu_core >= 0) {
        cpu_set_t cpuset;
        CPU_ZERO(&cpuset);
        CPU_SET(h->cfg.cpu_core, &cpuset);
        pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
    }

    const int BATCH_SIZE = h->cfg.ts_per_udp ? h->cfg.ts_per_udp : 7;
    uint8_t batch_buf[188 * BATCH_SIZE];

    while (atomic_load(&h->running)) {
        uint64_t head = atomic_load_explicit(&h->head, memory_order_acquire);
        uint64_t tail = atomic_load_explicit(&h->tail, memory_order_acquire);

        if (head - tail < (uint64_t)BATCH_SIZE) {
            struct timespec ts = {0, 100000}; /* 0.1ms */
            nanosleep(&ts, NULL);
            continue;
        }

        uint64_t now_ns = get_now_ns();
        uint64_t target_ns = h->ts_buffer[tail % RING_BUFFER_SIZE];

        if (now_ns >= target_ns) {
            /* Drift Protection: If lagging by > 100ms, force resync to avoid massive burst */
            if (now_ns > target_ns + 100000000ULL) {
                tsa_warn(TAG, "Critical lag detected (%lu ms). Resetting pacing baseline.",
                         (now_ns - target_ns) / 1000000ULL);
                h->base_pcr_ticks = INVALID_PCR;
                h->last_scheduled_ns = now_ns;
            }

            for (int i = 0; i < BATCH_SIZE; i++) {
                memcpy(batch_buf + i * 188, h->ring_buffer + ((tail + i) % RING_BUFFER_SIZE) * 188, 188);
            }

            if (h->srt_enabled) {
                srt_send(h->srt_sock, (const char*)batch_buf, sizeof(batch_buf));
            } else {
                sendto(h->fd, batch_buf, sizeof(batch_buf), 0, (struct sockaddr*)&h->dest_addr, sizeof(h->dest_addr));
            }

            tail += BATCH_SIZE;
            atomic_fetch_add(&h->total_ts_sent, BATCH_SIZE);
            atomic_fetch_add(&h->total_udp_sent, 1);
            atomic_store_explicit(&h->tail, tail, memory_order_release);
        } else {
            uint64_t diff = target_ns - now_ns;
            if (diff > 2000000ULL) {                     /* > 2ms */
                struct timespec sleep_ts = {0, 1000000}; /* 1ms sleep */
                nanosleep(&sleep_ts, NULL);
            } else if (diff > 500000ULL) { /* 0.5ms - 2ms */
                usleep(0);                 /* Yield slice */
            } else {
                /* Precise Spin-wait (< 0.5ms) */
                while (target_ns > now_ns) {
                    for (int i = 0; i < 50; i++) __asm__ __volatile__("pause");
                    now_ns = get_now_ns();
                }
            }
        }
    }
    return NULL;
}

tsp_handle_t* tsp_create(const tsp_config_t* cfg) {
    tsp_handle_t* h = calloc(1, sizeof(tsp_handle_t));
    if (!h) return NULL;
    h->cfg = *cfg;
    h->ring_buffer = malloc(RING_BUFFER_SIZE * 188);
    h->ts_buffer = malloc(RING_BUFFER_SIZE * sizeof(uint64_t));
    h->fd = socket(AF_INET, SOCK_DGRAM, 0);
    h->srt_sock = SRT_INVALID_SOCK;
    h->base_pcr_ticks = INVALID_PCR;
    h->last_pcr_val_tx = INVALID_PCR;
    if (cfg->url) {
        if (setup_srt(h, cfg->url) != 0) {
            free(h->ring_buffer);
            free(h->ts_buffer);
            if (h->fd >= 0) close(h->fd);
            free(h);
            return NULL;
        }
    } else if (cfg->dest_ip) {
        h->dest_addr.sin_family = AF_INET;
        h->dest_addr.sin_port = htons(cfg->port);
        inet_pton(AF_INET, cfg->dest_ip, &h->dest_addr.sin_addr.s_addr);
    }
    tsa_info(TAG, "tsp_create: bitrate=%lu, port=%d", cfg->bitrate, cfg->port);
    return h;
}

int tsp_enqueue(tsp_handle_t* h, const uint8_t* ts_packets, size_t count) {
    if (!h) return 0;
    uint64_t head = atomic_load_explicit(&h->head, memory_order_acquire);
    uint64_t tail = atomic_load_explicit(&h->tail, memory_order_acquire);
    if (RING_BUFFER_SIZE - (size_t)(head - tail) < count) return 0;

    uint64_t now_ns = get_now_ns();
    if (time(NULL) > h->last_pcr_reset_time + 10) h->base_pcr_ticks = INVALID_PCR;

    for (size_t i = 0; i < count; i++) {
        const uint8_t* pkt = ts_packets + (i * 188);
        uint64_t idx = (head + i) % RING_BUFFER_SIZE;
        memcpy(h->ring_buffer + idx * 188, pkt, 188);

        if ((pkt[3] & 0x20) && pkt[4] >= 7 && (pkt[5] & 0x10)) {
            uint64_t b = ((uint64_t)pkt[6] << 25) | ((uint64_t)pkt[7] << 17) | ((uint64_t)pkt[8] << 9) |
                         ((uint64_t)pkt[9] << 1) | (pkt[10] >> 7);
            uint64_t pcr = b * 300 + (((uint16_t)(pkt[10] & 0x01) << 8) | pkt[11]);

            if (pcr < h->last_pcr_val_tx || h->base_pcr_ticks == INVALID_PCR) {
                pacer_sync_wallclock(h, pcr, now_ns);
            } else {
                uint64_t pcr_delta = pcr - h->base_pcr_ticks;
                h->last_scheduled_ns = h->base_wall_ns + (pcr_delta * 1000ULL / 27ULL);
            }
            h->last_pcr_val_tx = pcr;
        } else {
            uint64_t br = h->cfg.bitrate ? h->cfg.bitrate : 10000000;
            uint64_t ns_per_pkt = (188ULL * 8ULL * 1000000000ULL) / br;
            h->last_scheduled_ns += ns_per_pkt;
        }
        h->ts_buffer[idx] = h->last_scheduled_ns;
    }
    atomic_store_explicit(&h->head, head + count, memory_order_release);
    return (int)count;
}

int tsp_start(tsp_handle_t* h) {
    atomic_store(&h->running, true);
    return pthread_create(&h->thread, NULL, tx_loop, h);
}
int tsp_stop(tsp_handle_t* h) {
    atomic_store(&h->running, false);
    pthread_join(h->thread, NULL);
    return 0;
}
void tsp_destroy(tsp_handle_t* h) {
    if (!h) return;
    if (atomic_load(&h->running)) tsp_stop(h);
    if (h->srt_enabled) srt_close(h->srt_sock);
    if (h->fd >= 0) close(h->fd);
    free(h->ring_buffer);
    free(h->ts_buffer);
    free(h);
}

/* --- API Implementation --- */
uint64_t tsp_get_detected_bitrate(tsp_handle_t* h) {
    return h->cfg.bitrate;
}
uint64_t tsp_get_bitrate(tsp_handle_t* h) {
    return h->cfg.bitrate;
}
uint64_t tsp_get_total_packets(tsp_handle_t* h) {
    return atomic_load(&h->total_ts_sent);
}
uint64_t tsp_get_udp_rate_scaled(tsp_handle_t* h) {
    int ts_per_udp = h->cfg.ts_per_udp ? h->cfg.ts_per_udp : 7;
    return h->cfg.bitrate / (ts_per_udp * 188 * 8);
}
pthread_t tsp_get_thread(tsp_handle_t* h) {
    return h->thread;
}
uint64_t tsp_debug_get_scheduled_time(tsp_handle_t* h, int idx) {
    uint64_t tail = atomic_load(&h->tail);
    return h->ts_buffer[(tail + idx) % RING_BUFFER_SIZE];
}

int tsp_get_stats(tsp_handle_t* h, uint64_t* total, int64_t* max_j, int64_t* min_j, uint64_t* drops, uint64_t* det_rate,
                  uint64_t* pps) {
    if (!h) return -1;
    if (total) *total = atomic_load(&h->total_ts_sent);
    if (max_j) *max_j = 0;
    if (min_j) *min_j = 0;
    if (drops) *drops = 0;
    if (det_rate) *det_rate = h->cfg.bitrate;
    if (pps) {
        uint64_t now = get_now_ns();
        uint64_t cur_t = atomic_load(&h->total_udp_sent);
        if (h->last_ns > 0 && now > h->last_ns) *pps = (cur_t - h->last_t) * 1000000000ULL / (now - h->last_ns);
        h->last_t = cur_t;
        h->last_ns = now;
    }
    return 0;
}

int tsp_get_stats_snapshot(tsp_handle_t* h, tsp_stats_t* s) {
    if (!h || !s) return -1;
    memset(s, 0, sizeof(tsp_stats_t));
    s->detected_bitrate = h->cfg.bitrate;
    s->total_packets = atomic_load(&h->total_ts_sent);
    s->timestamp_ns = get_now_ns();
    uint64_t cur_t = atomic_load(&h->total_udp_sent);
    if (h->last_ns > 0 && s->timestamp_ns > h->last_ns)
        s->pps = (cur_t - h->last_t) * 1000000000ULL / (s->timestamp_ns - h->last_ns);
    h->last_t = cur_t;
    h->last_ns = s->timestamp_ns;
    return 0;
}

spsc_ring_t* spsc_ring_create(size_t sz) {
    spsc_ring_t* r = calloc(1, sizeof(struct spsc_ring));
    r->sz = sz;
    r->elem_sz = 8;
    r->buffer = malloc(sz * 8);
    return r;
}
void spsc_ring_destroy(spsc_ring_t* r) {
    if (r) {
        free(r->buffer);
        free(r);
    }
}
int spsc_ring_push(spsc_ring_t* r, const uint8_t* data, size_t sz) {
    (void)sz;
    uint64_t head = atomic_load(&r->head), tail = atomic_load(&r->tail);
    if (head - tail >= r->sz) return -1;
    memcpy(r->buffer + (head % r->sz) * 8, data, 8);
    atomic_store(&r->head, head + 1);
    return 0;
}
int spsc_ring_pop(spsc_ring_t* r, uint8_t* data, size_t sz) {
    (void)sz;
    uint64_t head = atomic_load(&r->head), tail = atomic_load(&r->tail);
    if (head == tail) return -1;
    memcpy(data, r->buffer + (tail % r->sz) * 8, 8);
    atomic_store(&r->tail, tail + 1);
    return 0;
}
