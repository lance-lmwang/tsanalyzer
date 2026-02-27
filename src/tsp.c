#define _GNU_SOURCE
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
#include <sys/mman.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>
#include "tsp.h"

/* --- SRT Internal Setup --- */
static int setup_srt(tsp_handle_t* h, const char* url) {
    char host[256];
    int port, is_listener, latency, pbkeylen;
    char passphrase[128] = "";

    if (parse_srt_url_ext(url, host, &port, &is_listener, &latency, passphrase, &pbkeylen) != 0) return -1;

    srt_startup();
    h->srt_sock = srt_create_socket();
    if (h->srt_sock == SRT_INVALID_SOCK) return -1;

    int transtype = SRTT_LIVE;
    srt_setsockopt(h->srt_sock, 0, SRTO_TRANSTYPE, &transtype, sizeof(transtype));

    int timeout_ms = 500; 
    srt_setsockopt(h->srt_sock, 0, SRTO_SNDTIMEO, &timeout_ms, sizeof(timeout_ms));
    srt_setsockopt(h->srt_sock, 0, SRTO_RCVTIMEO, &timeout_ms, sizeof(timeout_ms));

    int sync = 0;
    srt_setsockopt(h->srt_sock, 0, SRTO_RCVSYN, &sync, sizeof(sync));
    srt_setsockopt(h->srt_sock, 0, SRTO_SNDSYN, &sync, sizeof(sync));

    srt_setsockopt(h->srt_sock, 0, SRTO_LATENCY, &latency, sizeof(latency));

    if (passphrase[0] != '\0') {
        srt_setsockopt(h->srt_sock, 0, SRTO_PASSPHRASE, passphrase, (int)strlen(passphrase));
        if (pbkeylen > 0) srt_setsockopt(h->srt_sock, 0, SRTO_PBKEYLEN, &pbkeylen, sizeof(int));
    }

    struct sockaddr_in sa;
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_port = htons(port);
    inet_pton(AF_INET, host[0] == '\0' ? "0.0.0.0" : host, &sa.sin_addr.s_addr);

    if (is_listener) {
        if (srt_bind(h->srt_sock, (struct sockaddr*)&sa, sizeof(sa)) == SRT_ERROR) return -1;
        srt_listen(h->srt_sock, 1);
        SRTSOCKET client = srt_accept(h->srt_sock, NULL, NULL);
        srt_close(h->srt_sock);
        h->srt_sock = client;
    } else {
        if (srt_connect(h->srt_sock, (struct sockaddr*)&sa, sizeof(sa)) == SRT_ERROR) return -1;
    }
    h->srt_enabled = true;
    return 0;
}

/* --- Core TSP Loop --- */
static void* tx_loop(void* arg) {
    tsp_handle_t* h = (tsp_handle_t*)arg;
    if (h->cfg.cpu_core >= 0) {
        cpu_set_t cpuset; CPU_ZERO(&cpuset); CPU_SET(h->cfg.cpu_core, &cpuset);
        pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
    }

    while (atomic_load(&h->running)) {
        uint64_t head = atomic_load_explicit(&h->head, memory_order_acquire);
        uint64_t tail = atomic_load_explicit(&h->tail, memory_order_acquire);
        
        if (head == tail) { usleep(100); continue; }

        // 发送逻辑 (简化版)
        uint8_t* pkt = h->ring_buffer + (tail % RING_BUFFER_SIZE) * TS_PACKET_SIZE;
        if (h->srt_enabled) {
            srt_send(h->srt_sock, (const char*)pkt, TS_PACKET_SIZE);
        } else {
            sendto(h->fd, pkt, TS_PACKET_SIZE, 0, (struct sockaddr*)&h->dest_addr, sizeof(h->dest_addr));
        }

        atomic_store_explicit(&h->tail, tail + 1, memory_order_release);
        atomic_fetch_add(&h->total_udp_packets, 1);
    }
    return NULL;
}

/* --- Public API --- */
tsp_handle_t* tsp_create(const tsp_config_t* cfg) {
    tsp_handle_t* h = calloc(1, sizeof(tsp_handle_t));
    if (!h || !cfg) return NULL;
    h->cfg = *cfg;
    h->ring_buffer = malloc(RING_BUFFER_SIZE * TS_PACKET_SIZE);
    h->fd = socket(AF_INET, SOCK_DGRAM, 0);
    
    if (cfg->srt_url) setup_srt(h, cfg->srt_url);
    else if (cfg->dest_ip) {
        h->dest_addr.sin_family = AF_INET;
        h->dest_addr.sin_port = htons(cfg->port);
        inet_pton(AF_INET, cfg->dest_ip, &h->dest_addr.sin_addr.s_addr);
    }

    return h;
}

int tsp_start(tsp_handle_t* h) {
    if (!h) return -1;
    atomic_store(&h->running, true);
    return pthread_create(&h->thread, NULL, tx_loop, h);
}

int tsp_stop(tsp_handle_t* h) {
    if (!h) return -1;
    atomic_store(&h->running, false);
    pthread_join(h->thread, NULL);
    return 0;
}

void tsp_destroy(tsp_handle_t* h) {
    if (!h) return;
    if (h->srt_enabled) srt_close(h->srt_sock);
    if (h->fd >= 0) close(h->fd);
    free(h->ring_buffer);
    free(h);
}

int tsp_enqueue(tsp_handle_t* h, const uint8_t* ts_packets, size_t count) {
    if (!h || !ts_packets) return -1;
    uint64_t head = atomic_load_explicit(&h->head, memory_order_relaxed);
    for (size_t i = 0; i < count; i++) {
        memcpy(h->ring_buffer + ((head + i) % RING_BUFFER_SIZE) * TS_PACKET_SIZE, 
               ts_packets + (i * TS_PACKET_SIZE), TS_PACKET_SIZE);
    }
    atomic_store_explicit(&h->head, head + count, memory_order_release);
    return 0;
}

uint64_t tsp_get_detected_bitrate(tsp_handle_t* h) {
    return h ? atomic_load(&h->detected_bitrate) : 0;
}

uint64_t tsp_get_total_packets(tsp_handle_t* h) {
    return h ? atomic_load(&h->total_udp_packets) : 0;
}

uint64_t tsp_get_udp_rate_scaled(tsp_handle_t* h) {
    return h ? atomic_load(&h->detected_bitrate) : 0;
}

pthread_t tsp_get_thread(tsp_handle_t* h) {
    return h ? h->thread : (pthread_t)0;
}

int tsp_get_stats(tsp_handle_t* h, uint64_t* total, int64_t* max_j, int64_t* min_j, uint64_t* drops, uint64_t* det_rate, uint64_t* pps) {
    if (!h) return -1;
    if (total) *total = atomic_load(&h->total_udp_packets);
    if (max_j) *max_j = 0;
    if (min_j) *min_j = 0;
    if (drops) *drops = 0;
    if (det_rate) *det_rate = atomic_load(&h->detected_bitrate);
    if (pps) *pps = 0;
    return 0;
}

int tsp_get_stats_snapshot(tsp_handle_t* h, tsp_stats_t* snap) {
    if (!h || !snap) return -1;
    memset(snap, 0, sizeof(tsp_stats_t));
    snap->total_packets = atomic_load(&h->total_udp_packets);
    snap->detected_bitrate = atomic_load(&h->detected_bitrate);
    return 0;
}

uint64_t calculate_target_time(tsp_handle_t* h, uint64_t pcr, uint64_t byte_off, uint64_t now) {
    (void)h; (void)pcr; (void)byte_off; return now;
}

struct spsc_ring { size_t sz; };
spsc_ring_t* spsc_ring_create(size_t sz) {
    spsc_ring_t* r = malloc(sizeof(spsc_ring_t));
    r->sz = sz; return r;
}
void spsc_ring_destroy(spsc_ring_t* r) { free(r); }
int spsc_ring_push(spsc_ring_t* r, const uint8_t* data, size_t sz) {
    (void)r; (void)data; (void)sz; return 0;
}
int spsc_ring_pop(spsc_ring_t* r, uint8_t* data, size_t sz) {
    (void)r; (void)data; (void)sz; return 0;
}
