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

/* SPSC Ring Mock Definition */
struct spsc_ring {
    uint8_t* buffer;
    size_t sz;
    size_t elem_sz;
    _Atomic uint64_t head;
    _Atomic uint64_t tail;
};

#include "tsa_log.h"
#define TAG "TSP"

static int setup_srt(tsp_handle_t* h, const char* url) {
    tsa_info(TAG, "Setting up SRT for url: %s", url);
    char host[256];
    int port, is_l, lat, pb;
    char pass[128] = "";
    if (parse_url_ext(url, host, &port, &is_l, &lat, pass, &pb) != 0) {
        tsa_error(TAG, "SRT: URL parse failed: %s", url);
        return -1;
    }

    srt_startup();
    h->srt_sock = srt_create_socket();

    // Industrial Standard Options (from SRS design)
    int transtype = SRTT_LIVE;
    int buf_size = 2000000;
    int tlpktdrop = 1;
    int tsbpd = 0;

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

    int sync = 1;
    srt_setsockopt(h->srt_sock, 0, SRTO_RCVSYN, &sync, sizeof(sync));
    srt_setsockopt(h->srt_sock, 0, SRTO_SNDSYN, &sync, sizeof(sync));

    struct sockaddr_in sa = {0};
    sa.sin_family = AF_INET;
    sa.sin_port = htons(port);
    inet_pton(AF_INET, (host[0] == '\0' || strcmp(host, "0.0.0.0") == 0) ? "0.0.0.0" : host, &sa.sin_addr.s_addr);

    if (is_l) {
        if (srt_bind(h->srt_sock, (struct sockaddr*)&sa, sizeof(sa)) == SRT_ERROR) {
            tsa_error(TAG, "SRT: Bind error: %s", srt_getlasterror_str());
            return -1;
        }
        srt_listen(h->srt_sock, 1);
        tsa_info(TAG, "SRT: Listener active on %s:%d", host[0] ? host : "0.0.0.0", port);
        SRTSOCKET c = srt_accept(h->srt_sock, NULL, NULL);
        if (c == SRT_INVALID_SOCK) return -1;
        srt_close(h->srt_sock);
        h->srt_sock = c;
    } else {
        tsa_info(TAG, "SRT: Connecting to %s:%d (latency=%dms)...", host[0] ? host : "127.0.0.1", port, lat);
        int retry = 3;
        while (retry--) {
            if (srt_connect(h->srt_sock, (struct sockaddr*)&sa, sizeof(sa)) != SRT_ERROR) break;
            if (retry == 0) {
                tsa_error(TAG, "SRT: Connect final failure: %s", srt_getlasterror_str());
                return -1;
            }
            tsa_info(TAG, "SRT: Handshake retry... (%d left)", retry);
            usleep(500000);
        }
    }
    h->srt_enabled = true;
    return 0;
}

static void* tx_loop(void* arg) {
    tsp_handle_t* h = (tsp_handle_t*)arg;
    tsa_info(TAG, "tx_loop thread started for h=%p", h);
    if (h->cfg.cpu_core >= 0) {
        cpu_set_t cpuset;
        CPU_ZERO(&cpuset);
        CPU_SET(h->cfg.cpu_core, &cpuset);
        pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
    }

    const int BATCH_SIZE = h->cfg.ts_per_udp ? h->cfg.ts_per_udp : 7;
    uint8_t batch_buf[TS_PACKET_SIZE * BATCH_SIZE];
    time_t last_stats_sec = 0;

    while (atomic_load(&h->running)) {
        uint64_t head = atomic_load_explicit(&h->head, memory_order_acquire);
        uint64_t tail = atomic_load_explicit(&h->tail, memory_order_acquire);

        if (head - tail < (uint64_t)BATCH_SIZE) {
            usleep(100);
            continue;
        }

        struct timespec now_ts;
        clock_gettime(CLOCK_MONOTONIC, &now_ts);
        uint64_t now_ns = (uint64_t)now_ts.tv_sec * 1000000000ULL + now_ts.tv_nsec;

        // Check if the next batch is ready to be sent
        uint64_t target_ns = h->ts_buffer[tail % RING_BUFFER_SIZE];

        if (now_ns >= target_ns) {
            if (now_ns > target_ns + 50000000ULL) { // More than 50ms late
                static uint64_t last_warn_pkts = 0;
                uint64_t cur_pkts = atomic_load(&h->total_ts_sent);
                if (cur_pkts > last_warn_pkts + 10000) {
                    tsa_warn(TAG, "Pacer lagging behind schedule by %lu ms (buffer pressure or CPU load)",
                             (now_ns - target_ns) / 1000000);
                    last_warn_pkts = cur_pkts;
                }
            }
            // Precision dispatch: send exactly BATCH_SIZE packets
            for (int i = 0; i < BATCH_SIZE; i++) {
                memcpy(batch_buf + i * TS_PACKET_SIZE, h->ring_buffer + ((tail + i) % RING_BUFFER_SIZE) * TS_PACKET_SIZE,
                       TS_PACKET_SIZE);
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
            // Ahead of schedule: wait
            uint64_t diff = target_ns - now_ns;
            if (diff > 1000000ULL) { // > 1ms, safe to sleep
                struct timespec ts = {0, 500000}; // 0.5ms sleep
                nanosleep(&ts, NULL);
            } else {
                // Microsecond precision: busy wait
                while (target_ns > now_ns) {
                    for (int i = 0; i < 10; i++) __asm__ __volatile__("pause");
                    clock_gettime(CLOCK_MONOTONIC, &now_ts);
                    now_ns = (uint64_t)now_ts.tv_sec * 1000000000ULL + now_ts.tv_nsec;
                }
            }
        }

        if (now_ts.tv_sec != last_stats_sec) {
            tsp_stats_t snap;
            tsp_get_stats_snapshot(h, &snap);
            if (h->cfg.stats_cb) {
                h->cfg.stats_cb(h, &snap, h->cfg.user_data);
            }
            last_stats_sec = now_ts.tv_sec;
        }
    }
    tsa_info(TAG, "tx_loop thread exiting for h=%p", h);
    return NULL;
}

tsp_handle_t* tsp_create(const tsp_config_t* cfg) {
    tsa_info(TAG, "tsp_create: bitrate=%lu, port=%d", cfg->bitrate, cfg->port);
    tsp_handle_t* h = calloc(1, sizeof(tsp_handle_t));
    if (!h) return NULL;
    h->cfg = *cfg;
    h->ring_buffer = malloc(RING_BUFFER_SIZE * TS_PACKET_SIZE);
    h->ts_buffer = malloc(RING_BUFFER_SIZE * sizeof(uint64_t));
    h->fd = socket(AF_INET, SOCK_DGRAM, 0);
    h->last_pcr_val_tx = INVALID_PCR;
    h->last_ns = 0;
    h->last_t = 0;
    tsa_pcr_clock_init(&h->clk);

    if (cfg->url) {
        if (setup_srt(h, cfg->url) != 0) {
            tsa_error(TAG, "SRT setup failed");
            free(h->ring_buffer);
            if (h->fd >= 0) close(h->fd);
            free(h);
            return NULL;
        }
    } else if (cfg->dest_ip) {
        h->dest_addr.sin_family = AF_INET;
        h->dest_addr.sin_port = htons(cfg->port);
        inet_pton(AF_INET, cfg->dest_ip, &h->dest_addr.sin_addr.s_addr);
    }
    return h;
}

int tsp_enqueue(tsp_handle_t* h, const uint8_t* ts_packets, size_t count) {
    if (!h) return 0;
    uint64_t head = atomic_load_explicit(&h->head, memory_order_acquire);
    uint64_t tail = atomic_load_explicit(&h->tail, memory_order_acquire);

    size_t available = RING_BUFFER_SIZE - (size_t)(head - tail);
    if (available == 0) return 0;
    if (count > available) count = available;

    struct timespec now_ts;
    clock_gettime(CLOCK_MONOTONIC, &now_ts);
    uint64_t now_ns = (uint64_t)now_ts.tv_sec * 1000000000ULL + now_ts.tv_nsec;

    uint64_t br = atomic_load(&h->detected_bitrate);
    if (br == 0) br = h->cfg.bitrate ? h->cfg.bitrate : 8000000;
    uint64_t ns_per_pkt = (1504ULL * 1000000000ULL) / br;

    for (size_t i = 0; i < count; i++) {
        const uint8_t* pkt = ts_packets + (i * TS_PACKET_SIZE);
        uint64_t idx = (head + i) % RING_BUFFER_SIZE;
        memcpy(h->ring_buffer + idx * TS_PACKET_SIZE, pkt, TS_PACKET_SIZE);

        uint64_t target_ns = 0;
        if ((pkt[3] & 0x20) && pkt[4] > 0 && (pkt[5] & 0x10)) {
            uint64_t b = ((uint64_t)pkt[6] << 25) | ((uint64_t)pkt[7] << 17) | ((uint64_t)pkt[8] << 9) |
                         ((uint64_t)pkt[9] << 1) | (pkt[10] >> 7);
            uint64_t pcr = b * 300 + (((uint16_t)(pkt[10] & 0x01) << 8) | pkt[11]);

            tsa_pcr_clock_update(&h->clk, pcr, now_ns);

            // Map PCR to system time with 100ms latency buffer
            target_ns = tsa_pcr_clock_pcr_to_sys(&h->clk, pcr) + 100000000ULL;

            // Update detected bitrate for interpolation between PCRs
            if (h->last_pcr_val_tx != INVALID_PCR && pcr > h->last_pcr_val_tx) {
                uint64_t dt_pcr_ns = (pcr - h->last_pcr_val_tx) * 1000 / 27;
                uint64_t dp = head + i + 1 - h->pkts_since_pcr;
                if (dt_pcr_ns > 0) {
                    uint64_t old_br = atomic_load(&h->detected_bitrate);
                    uint64_t new_br = dp * 1504ULL * 1000000000ULL / dt_pcr_ns;
                    atomic_store(&h->detected_bitrate, new_br);
                    if (old_br == 0) {
                        tsa_info(TAG, "Bitrate converged: %.2f Mbps (Drift: %.1f PPM)",
                                 (double)new_br / 1000000.0, h->clk.drift_ppm);
                    }
                }
            }
            h->last_pcr_val_tx = pcr;
            h->pkts_since_pcr = head + i + 1;
            h->last_scheduled_ns = target_ns;
        } else {
            // Interpolate based on last scheduled time
            if (h->last_scheduled_ns == 0) {
                h->last_scheduled_ns = now_ns + 100000000ULL;
            }
            target_ns = h->last_scheduled_ns + ns_per_pkt;
            h->last_scheduled_ns = target_ns;
        }
        h->ts_buffer[idx] = target_ns;
    }
    atomic_store_explicit(&h->head, head + count, memory_order_release);
    return (int)count;
}

int tsp_start(tsp_handle_t* h) {
    if (!h) return -1;
    tsa_info(TAG, "tsp_start: h=%p", h);
    atomic_store(&h->running, true);
    return pthread_create(&h->thread, NULL, tx_loop, h);
}
int tsp_stop(tsp_handle_t* h) {
    if (!h) return -1;
    tsa_info(TAG, "tsp_stop: h=%p", h);
    atomic_store(&h->running, false);
    pthread_join(h->thread, NULL);
    return 0;
}
void tsp_destroy(tsp_handle_t* h) {
    if (!h) return;
    tsa_info(TAG, "tsp_destroy: h=%p", h);
    if (atomic_load(&h->running)) {
        tsp_stop(h);
    }
    if (h->srt_enabled) srt_close(h->srt_sock);
    if (h->fd >= 0) close(h->fd);
    free(h->ring_buffer);
    free(h->ts_buffer);
    free(h);
}

uint64_t tsp_get_detected_bitrate(tsp_handle_t* h) {
    return atomic_load(&h->detected_bitrate);
}

uint64_t tsp_get_total_packets(tsp_handle_t* h) {
    return atomic_load(&h->total_ts_sent);
}

uint64_t tsp_get_udp_rate_scaled(tsp_handle_t* h) {
    uint64_t br = atomic_load(&h->detected_bitrate);
    return br / (7 * 188 * 8);
}

void tsp_update_bitrate(tsp_handle_t* h, uint64_t nb) {
    h->cfg.bitrate = nb;
}

uint64_t tsp_get_bitrate(tsp_handle_t* h) {
    return h->cfg.bitrate;
}

pthread_t tsp_get_thread(tsp_handle_t* h) {
    return h->thread;
}

int tsp_get_stats(tsp_handle_t* h, uint64_t* t, int64_t* mx, int64_t* mn, uint64_t* d, uint64_t* dr, uint64_t* pps) {
    if (!h) return -1;
    (void)mx; (void)mn; (void)d;
    if (t) *t = atomic_load(&h->total_ts_sent);
    if (dr) *dr = atomic_load(&h->detected_bitrate);

    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    uint64_t now_ns = (uint64_t)now.tv_sec * 1000000000ULL + now.tv_nsec;

    uint64_t cur_t = atomic_load(&h->total_udp_sent);
    if (pps) {
        if (h->last_ns > 0 && now_ns > h->last_ns) {
            *pps = (cur_t - h->last_t) * 1000000000ULL / (now_ns - h->last_ns);
        } else {
            *pps = 0;
        }
    }
    h->last_t = cur_t;
    h->last_ns = now_ns;

    return 0;
}

int tsp_get_stats_snapshot(tsp_handle_t* h, tsp_stats_t* s) {
    if (!h || !s) return -1;
    memset(s, 0, sizeof(tsp_stats_t));
    s->detected_bitrate = atomic_load(&h->detected_bitrate);
    s->total_packets = atomic_load(&h->total_ts_sent);
    return 0;
}
uint64_t calculate_target_time(tsp_handle_t* h, uint64_t p, uint64_t b, uint64_t n) {
    (void)h;
    (void)p;
    (void)b;
    return n;
}
spsc_ring_t* spsc_ring_create(size_t s) {
    spsc_ring_t* r = calloc(1, sizeof(struct spsc_ring));
    r->sz = s;
    r->elem_sz = 8;
    r->buffer = malloc(s * 8);
    return r;
}
void spsc_ring_destroy(spsc_ring_t* r) {
    if (r) {
        free(r->buffer);
        free(r);
    }
}
int spsc_ring_push(spsc_ring_t* r, const uint8_t* d, size_t s) {
    (void)s;
    uint64_t head = atomic_load(&r->head), tail = atomic_load(&r->tail);
    if (head - tail >= r->sz) return -1;
    memcpy(r->buffer + (head % r->sz) * 8, d, 8);
    atomic_store(&r->head, head + 1);
    return 0;
}
int spsc_ring_pop(spsc_ring_t* r, uint8_t* d, size_t s) {
    (void)s;
    uint64_t head = atomic_load(&r->head), tail = atomic_load(&r->tail);
    if (head == tail) return -1;
    memcpy(d, r->buffer + (tail % r->sz) * 8, 8);
    atomic_store(&r->tail, tail + 1);
    return 0;
}
