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

static int setup_srt(tsp_handle_t* h, const char* url) {
    char host[256]; int port, is_l, lat, pb; char pass[128] = "";
    if (parse_srt_url_ext(url, host, &port, &is_l, &lat, pass, &pb) != 0) return -1;
    srt_startup(); h->srt_sock = srt_create_socket();
    int live = SRTT_LIVE; srt_setsockopt(h->srt_sock, 0, SRTO_TRANSTYPE, &live, sizeof(live));
    struct sockaddr_in sa = {.sin_family = AF_INET, .sin_port = htons(port)};
    inet_pton(AF_INET, host[0] == '\0' ? "0.0.0.0" : host, &sa.sin_addr.s_addr);
    if (is_l) { srt_bind(h->srt_sock, (struct sockaddr*)&sa, sizeof(sa)); srt_listen(h->srt_sock, 1);
        SRTSOCKET c = srt_accept(h->srt_sock, NULL, NULL); srt_close(h->srt_sock); h->srt_sock = c;
    } else srt_connect(h->srt_sock, (struct sockaddr*)&sa, sizeof(sa));
    h->srt_enabled = true; return 0;
}

static void* tx_loop(void* arg) {
    tsp_handle_t* h = (tsp_handle_t*)arg;
    if (h->cfg.cpu_core >= 0) {
        cpu_set_t cpuset; CPU_ZERO(&cpuset); CPU_SET(h->cfg.cpu_core, &cpuset);
        pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
    }
    uint64_t last_token_ns = 0; double tokens = 0;
    const uint64_t BUSY_WAIT_THRESHOLD_NS = 2000000; // 2ms

    while (atomic_load(&h->running)) {
        uint64_t head = atomic_load_explicit(&h->head, memory_order_acquire);
        uint64_t tail = atomic_load_explicit(&h->tail, memory_order_acquire);
        if (head == tail) {
            usleep(100);
            continue;
        }

        struct timespec ts_now;
        clock_gettime(CLOCK_MONOTONIC, &ts_now);
        uint64_t now = (uint64_t)ts_now.tv_sec * 1e9 + ts_now.tv_nsec;
        if (last_token_ns == 0) last_token_ns = now;

        uint64_t br = atomic_load(&h->detected_bitrate);
        if (br == 0) br = h->cfg.bitrate ? h->cfg.bitrate : 8000000;

        // Token bucket: 1 token = 1 bit
        tokens += (double)(now - last_token_ns) * br / 1e9;
        last_token_ns = now;

        // Cap bucket size to 10ms of traffic to prevent huge bursts after pauses
        double max_tokens = (double)br * 0.01;
        if (tokens > max_tokens) tokens = max_tokens;

        uint64_t packet_bits = TS_PACKET_SIZE * 8;
        if (tokens < (double)packet_bits) {
            // Calculate time needed until we have enough tokens for one packet
            double needed_bits = (double)packet_bits - tokens;
            uint64_t wait_ns = (uint64_t)(needed_bits * 1e9 / br);

            if (wait_ns >= BUSY_WAIT_THRESHOLD_NS) {
                struct timespec ts_wait;
                ts_wait.tv_sec = ts_now.tv_sec;
                ts_wait.tv_nsec = ts_now.tv_nsec + wait_ns;
                if (ts_wait.tv_nsec >= 1000000000L) {
                    ts_wait.tv_sec++;
                    ts_wait.tv_nsec -= 1000000000L;
                }
                clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &ts_wait, NULL);
            } else if (wait_ns > 500) {
                // Short wait: busy-wait with pause to save power and be precise
                for (int i = 0; i < 100; i++) __asm__ volatile("pause" ::: "memory");
            }
            continue;
        }

        uint8_t* pkt = h->ring_buffer + (tail % RING_BUFFER_SIZE) * TS_PACKET_SIZE;
        if (h->srt_enabled) srt_send(h->srt_sock, (const char*)pkt, TS_PACKET_SIZE);
        else sendto(h->fd, pkt, TS_PACKET_SIZE, 0, (struct sockaddr*)&h->dest_addr, sizeof(h->dest_addr));

        tokens -= (double)packet_bits;
        atomic_store_explicit(&h->tail, tail + 1, memory_order_release);
        atomic_fetch_add(&h->total_udp_packets, 1);
    }
    return NULL;
}

tsp_handle_t* tsp_create(const tsp_config_t* cfg) {
    tsp_handle_t* h = calloc(1, sizeof(tsp_handle_t));
    h->cfg = *cfg; h->ring_buffer = malloc(RING_BUFFER_SIZE * TS_PACKET_SIZE);
    h->fd = socket(AF_INET, SOCK_DGRAM, 0);
    h->last_pcr_val_tx = INVALID_PCR;
    if (cfg->srt_url) setup_srt(h, cfg->srt_url);
    else if (cfg->dest_ip) {
        h->dest_addr.sin_family = AF_INET; h->dest_addr.sin_port = htons(cfg->port);
        inet_pton(AF_INET, cfg->dest_ip, &h->dest_addr.sin_addr.s_addr);
    }
    return h;
}

int tsp_enqueue(tsp_handle_t* h, const uint8_t* ts_packets, size_t count) {
    uint64_t head = atomic_load_explicit(&h->head, memory_order_relaxed);
    for (size_t i = 0; i < count; i++) {
        const uint8_t* pkt = ts_packets + (i * TS_PACKET_SIZE);
        memcpy(h->ring_buffer + ((head + i) % RING_BUFFER_SIZE) * TS_PACKET_SIZE, pkt, TS_PACKET_SIZE);
        if ((pkt[3] & 0x20) && pkt[4] > 0 && (pkt[5] & 0x10)) {
            uint64_t b = ((uint64_t)pkt[6] << 25) | ((uint64_t)pkt[7] << 17) | ((uint64_t)pkt[8] << 9) | ((uint64_t)pkt[9] << 1) | (pkt[10] >> 7);
            uint64_t pcr = b * 300 + (((uint16_t)(pkt[10] & 0x01) << 8) | pkt[11]);
            if (h->last_pcr_val_tx != INVALID_PCR && pcr > h->last_pcr_val_tx) {
                uint64_t dt_pcr_ns = (pcr - h->last_pcr_val_tx) * 1000 / 27;
                uint64_t dp = head + i + 1 - h->pkts_since_pcr;
                if (dt_pcr_ns > 0) atomic_store(&h->detected_bitrate, dp * 1504ULL * 1000000000ULL / dt_pcr_ns);
            }
            h->last_pcr_val_tx = pcr; h->pkts_since_pcr = head + i + 1;
        }
    }
    atomic_store_explicit(&h->head, head + count, memory_order_release);
    return 0;
}

int tsp_start(tsp_handle_t* h) { atomic_store(&h->running, true); return pthread_create(&h->thread, NULL, tx_loop, h); }
int tsp_stop(tsp_handle_t* h) { atomic_store(&h->running, false); pthread_join(h->thread, NULL); return 0; }
void tsp_destroy(tsp_handle_t* h) { if (!h) return; if (h->srt_enabled) srt_close(h->srt_sock); if (h->fd >= 0) close(h->fd); free(h->ring_buffer); free(h); }
uint64_t tsp_get_detected_bitrate(tsp_handle_t* h) { return atomic_load(&h->detected_bitrate); }
uint64_t tsp_get_total_packets(tsp_handle_t* h) { return atomic_load(&h->total_udp_packets); }
uint64_t tsp_get_udp_rate_scaled(tsp_handle_t* h) { uint64_t br = atomic_load(&h->detected_bitrate); return br / (7 * 188 * 8); }
void tsp_update_bitrate(tsp_handle_t* h, uint64_t nb) { h->cfg.bitrate = nb; }
uint64_t tsp_get_bitrate(tsp_handle_t* h) { return h->cfg.bitrate; }
pthread_t tsp_get_thread(tsp_handle_t* h) { return h->thread; }
int tsp_get_stats(tsp_handle_t* h, uint64_t* t, int64_t* mx, int64_t* mn, uint64_t* d, uint64_t* dr, uint64_t* pps) {
    (void)mx; (void)mn; (void)d; (void)pps;
    if (t) *t = atomic_load(&h->total_udp_packets);
    if (dr) *dr = atomic_load(&h->detected_bitrate);
    return 0;
}
int tsp_get_stats_snapshot(tsp_handle_t* h, tsp_stats_t* s) { s->detected_bitrate = atomic_load(&h->detected_bitrate); return 0; }
uint64_t calculate_target_time(tsp_handle_t* h, uint64_t p, uint64_t b, uint64_t n) {
    (void)h; (void)p; (void)b;
    return n;
}
spsc_ring_t* spsc_ring_create(size_t s) { spsc_ring_t* r = calloc(1, sizeof(struct spsc_ring)); r->sz = s; r->elem_sz = 8; r->buffer = malloc(s * 8); return r; }
void spsc_ring_destroy(spsc_ring_t* r) { if (r) { free(r->buffer); free(r); } }
int spsc_ring_push(spsc_ring_t* r, const uint8_t* d, size_t s) {
    (void)s;
    uint64_t head = atomic_load(&r->head), tail = atomic_load(&r->tail);
    if (head - tail >= r->sz) {
        return -1;
    }
    memcpy(r->buffer + (head % r->sz) * 8, d, 8);
    atomic_store(&r->head, head + 1); return 0;
}
int spsc_ring_pop(spsc_ring_t* r, uint8_t* d, size_t s) {
    (void)s;
    uint64_t head = atomic_load(&r->head), tail = atomic_load(&r->tail);
    if (head == tail) {
        return -1;
    }
    memcpy(d, r->buffer + (tail % r->sz) * 8, 8);
    atomic_store(&r->tail, tail + 1); return 0;
}
