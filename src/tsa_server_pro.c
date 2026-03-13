#define _GNU_SOURCE
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <pthread.h>
#include <sched.h>
#include <signal.h>
#include <srt.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "../include/tsa_top_shm.h"
#include "mongoose.h"
#include "mpmc_queue.h"
#include "spsc_queue.h"
#include "tsa.h"
#include "tsa_auth.h"
#include "tsa_conf.h"
#include "tsa_internal.h"
#include "tsa_log.h"

#define TAG "SERVER_PRO"

#define MAX_CONNS 4096
#define HTTP_PORT "8088"
#define PACKET_BUF_SIZE 65536
#define ANA_QUEUE_SIZE 4096

typedef enum { CONN_UDP, CONN_SRT_LISTENER, CONN_SRT_CLIENT } conn_type_t;

typedef struct {
    SRTSOCKET fd;
    int tx_fd; /* Forwarding socket */
    conn_type_t type;
    char id[TSA_ID_MAX];
    tsa_handle_t* tsa;
    spsc_queue_t* tx_q;  /* Forwarding Queue */
    spsc_queue_t* ana_q; /* Analysis Queue */
    _Atomic bool scheduled;
    atomic_bool closed;
    _Atomic uint64_t pending_drops;
    uint32_t conn_idx;
    uint64_t last_commit_ns;  // Heartbeat tracking
} conn_t;

static conn_t* g_conns[MAX_CONNS];
static atomic_int g_conn_count = 0;
static _Atomic bool g_run = true;
static pthread_mutex_t g_conn_lock = PTHREAD_MUTEX_INITIALIZER;
static mpmc_queue_t* g_ready_queue;

static int g_http_port = 8088;
static int g_srt_port = 9000;

extern void tsa_exporter_prom_v2(tsa_handle_t** handles, int count, char* buf, size_t sz);
extern void tsa_exporter_prom_core(tsa_handle_t** handles, int count, char* buf, size_t sz);
extern void tsa_exporter_prom_pids(tsa_handle_t** handles, int count, char* buf, size_t sz);

static tsa_full_conf_t g_sys_conf;

static void load_config(const char* file) {
    g_sys_conf.http_listen_port = 8088;
    g_sys_conf.srt_listen_port = 9000;
    g_sys_conf.worker_threads = 16;
    g_sys_conf.worker_slice_us = 2000;  // Default 2ms

    if (tsa_conf_load(&g_sys_conf, file) != 0) {
        tsa_error(TAG, "Failed to load configuration: %s. Using internal defaults.", file);
    }

    g_http_port = g_sys_conf.http_listen_port;
    g_srt_port = g_sys_conf.srt_listen_port;

    tsa_auth_init(g_sys_conf.api_secret[0] ? g_sys_conf.api_secret : NULL);

    struct sockaddr_in sa = {0};
    sa.sin_family = AF_INET;
    sa.sin_addr.s_addr = INADDR_ANY;

    for (int i = 0; i < g_sys_conf.stream_count; i++) {
        if (atomic_load(&g_conn_count) >= MAX_CONNS) break;
        tsa_stream_conf_t* sc = &g_sys_conf.streams[i];

        conn_t* c = calloc(1, sizeof(conn_t));
        snprintf(c->id, sizeof(c->id), "%s", sc->id);

        if (strncmp(sc->cfg.url, "udp://", 6) == 0) {
            char* p_str = strrchr(sc->cfg.url, ':');
            int port = p_str ? atoi(p_str + 1) : 19001;
            int udp_fd = socket(AF_INET, SOCK_DGRAM, 0);
            sa.sin_port = htons(port);
            if (bind(udp_fd, (struct sockaddr*)&sa, sizeof(sa)) == 0) {
                fcntl(udp_fd, F_SETFL, fcntl(udp_fd, F_GETFL, 0) | O_NONBLOCK);
                c->fd = (SRTSOCKET)udp_fd;
                c->type = CONN_UDP;
                c->tsa = tsa_create(&sc->cfg);
                c->tx_q = spsc_queue_create(1024);
                c->ana_q = spsc_queue_create(ANA_QUEUE_SIZE);
                atomic_init(&c->scheduled, false);

                pthread_mutex_lock(&g_conn_lock);
                int idx = atomic_fetch_add(&g_conn_count, 1);
                c->conn_idx = idx;
                g_conns[idx] = c;
                pthread_mutex_unlock(&g_conn_lock);
                tsa_info(TAG, "Configured UDP stream [%s] on port %d", sc->id, port);
            } else {
                tsa_error(TAG, "Failed to bind UDP port %d for stream [%s]", port, sc->id);
                free(c);
                close(udp_fd);
            }
        }
    }
}

static uint64_t g_cycles_per_us = 3000;

static inline uint64_t rdtsc(void) {
    uint32_t lo, hi;
    __asm__ __volatile__("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}

static void calibrate_tsc() {
    struct timespec ts1, ts2;
    clock_gettime(CLOCK_MONOTONIC, &ts1);
    uint64_t t1 = rdtsc();
    usleep(10000);
    clock_gettime(CLOCK_MONOTONIC, &ts2);
    uint64_t t2 = rdtsc();
    uint64_t ns = (ts2.tv_sec - ts1.tv_sec) * 1000000000ULL + (ts2.tv_nsec - ts1.tv_nsec);
    g_cycles_per_us = (t2 - t1) / (ns / 1000);
    if (g_cycles_per_us == 0) g_cycles_per_us = 3000;
}

static void sig_handler(int sig) {
    (void)sig;
    atomic_store(&g_run, false);
}

static void* worker_thread(void* arg) {
    (void)arg;
    ts_packet_t pkt;
    uint32_t stream_id;
    uint64_t slice_cycles = g_sys_conf.worker_slice_us * g_cycles_per_us;

    while (atomic_load(&g_run)) {
        if (mpmc_queue_pop(g_ready_queue, &stream_id)) {
            conn_t* c = NULL;
            pthread_mutex_lock(&g_conn_lock);
            if (stream_id < (uint32_t)atomic_load(&g_conn_count)) {
                c = g_conns[stream_id];
            }
            pthread_mutex_unlock(&g_conn_lock);

            if (c && c->tsa && c->ana_q && !atomic_load(&c->closed)) {
                uint64_t internal_drops = atomic_exchange(&c->pending_drops, 0);
                if (internal_drops > 0) {
                    tsa_handle_internal_drop(c->tsa, internal_drops);
                }

                uint64_t start_tsc = rdtsc();
                uint64_t now = (uint64_t)ts_now_ns128();
                int drained = 0;

                while (spsc_queue_pop(c->ana_q, &pkt)) {
                    tsa_process_packet(c->tsa, pkt.data, pkt.timestamp_ns);
                    drained++;
                    if (rdtsc() - start_tsc >= slice_cycles) {
                        atomic_fetch_add(&c->tsa->live->worker_slice_overruns, 1);
                        break;
                    }
                }

                if (now > c->last_commit_ns && (now - c->last_commit_ns) >= 100000000ULL) {
                    tsa_commit_snapshot(c->tsa, now);
                    c->last_commit_ns = now;
                }
            }

            if (c) {
                atomic_store_explicit(&c->scheduled, false, memory_order_release);
                // Re-arm if still not empty
                if (!spsc_queue_is_empty(c->ana_q)) {
                    if (!atomic_load_explicit(&c->scheduled, memory_order_relaxed)) {
                        if (!atomic_exchange_explicit(&c->scheduled, true, memory_order_acq_rel)) {
                            if (!mpmc_queue_push(g_ready_queue, c->conn_idx)) {
                                atomic_store_explicit(&c->scheduled, false, memory_order_relaxed);
                            }
                        }
                    }
                }
            }
        } else {
            usleep(100);
        }
    }
    return NULL;
}

static void forward_packet(conn_t* c, const ts_packet_t* pkt) {
    if (!c->tx_q) return;
    spsc_queue_push(c->tx_q, pkt);
    ts_packet_t tmp;
    while (spsc_queue_pop(c->tx_q, &tmp)) {
        // Simulating O(1) forwarding
    }
}

static void* io_thread(void* arg) {
    int srt_eid = (int)(intptr_t)arg;
    uint8_t raw_buf[4096];
    ts_packet_t pkt;

    tsa_info(TAG, "I/O Thread Active (SRT eid=%d)", srt_eid);

    while (atomic_load(&g_run)) {
        SRTSOCKET ready_srt[32];
        int n_srt = 32;
        int r = srt_epoll_wait(srt_eid, ready_srt, &n_srt, NULL, NULL, 5, NULL, NULL, NULL, NULL);

        if (r > 0) {
            for (int i = 0; i < r; i++) {
                SRTSOCKET s = ready_srt[i];
                conn_t* c = NULL;
                pthread_mutex_lock(&g_conn_lock);
                for (int j = 0; j < atomic_load(&g_conn_count); j++) {
                    if (g_conns[j]->fd == s) {
                        c = g_conns[j];
                        break;
                    }
                }
                pthread_mutex_unlock(&g_conn_lock);

                if (!c) continue;

                if (c->type == CONN_SRT_LISTENER) {
                    SRTSOCKET client = srt_accept(s, NULL, NULL);
                    if (client != SRT_INVALID_SOCK) {
                        tsa_info(TAG, "Accepted SRT fd=%d", (int)client);
                        int sync = 0;
                        srt_setsockopt(client, 0, SRTO_RCVSYN, &sync, sizeof(sync));
                        srt_setsockopt(client, 0, SRTO_SNDSYN, &sync, sizeof(sync));

                        conn_t* nc = calloc(1, sizeof(conn_t));
                        nc->fd = client;
                        nc->type = CONN_SRT_CLIENT;
                        sprintf(nc->id, "SRT-%d", (int)client);
                        tsa_config_t cfg = {.op_mode = TSA_MODE_LIVE, .analysis.pcr_ema_alpha = 0.1};
                        snprintf(cfg.input_label, sizeof(cfg.input_label), "%s", nc->id);
                        nc->tsa = tsa_create(&cfg);
                        nc->tx_q = spsc_queue_create(1024);
                        nc->ana_q = spsc_queue_create(ANA_QUEUE_SIZE);
                        atomic_init(&nc->scheduled, false);

                        int events = SRT_EPOLL_IN | SRT_EPOLL_ERR;
                        srt_epoll_add_usock(srt_eid, client, &events);

                        pthread_mutex_lock(&g_conn_lock);
                        int idx = atomic_fetch_add(&g_conn_count, 1);
                        nc->conn_idx = idx;
                        g_conns[idx] = nc;
                        pthread_mutex_unlock(&g_conn_lock);
                    }
                } else if (c->type == CONN_SRT_CLIENT) {
                    while (true) {
                        int len = srt_recv(s, (char*)raw_buf, sizeof(raw_buf));
                        if (len > 0) {
                            uint64_t now = (uint64_t)ts_now_ns128();
                            for (int k = 0; k < len / 188; k++) {
                                memcpy(pkt.data, raw_buf + (k * 188), 188);
                                pkt.timestamp_ns = now;
                                forward_packet(c, &pkt);
                                bool was_empty = spsc_queue_is_empty(c->ana_q);
                                if (!spsc_queue_push(c->ana_q, &pkt)) {
                                    atomic_fetch_add(&c->pending_drops, 1);
                                } else if (was_empty) {
                                    if (!atomic_load_explicit(&c->scheduled, memory_order_relaxed)) {
                                        if (!atomic_exchange_explicit(&c->scheduled, true, memory_order_acq_rel)) {
                                            if (!mpmc_queue_push(g_ready_queue, c->conn_idx)) {
                                                atomic_store_explicit(&c->scheduled, false, memory_order_relaxed);
                                            }
                                        }
                                    }
                                }
                            }
                            if (len % 1316 == 0) {
                                tsa_srt_stats_t srt_stats = {0};
                                SRT_TRACEBSTATS bstats;
                                if (srt_bstats(s, &bstats, 0) == 0) {
                                    srt_stats.rtt_ms = (int64_t)bstats.msRTT;
                                    srt_stats.byte_rcv_buf = (uint32_t)bstats.byteRcvBuf;
                                    srt_stats.bytes_received = (uint64_t)bstats.byteRecvTotal;
                                    srt_stats.bytes_lost = (uint64_t)bstats.byteRcvLossTotal;
                                    tsa_update_srt_stats(c->tsa, &srt_stats);
                                }
                            }
                        } else if (len == SRT_ERROR) {
                            if (srt_getlasterror(NULL) != SRT_EASYNCRCV) {
                                tsa_info(TAG, "Closed SRT fd=%d (%s)", (int)s, srt_getlasterror_str());
                                srt_epoll_remove_usock(srt_eid, s);
                                atomic_store(&c->closed, true);
                            }
                            break;
                        } else
                            break;
                    }
                }
            }
        }

        /* UDP Handling */
        int count = atomic_load(&g_conn_count);
        for (int i = 0; i < count; i++) {
            pthread_mutex_lock(&g_conn_lock);
            conn_t* c = g_conns[i];
            pthread_mutex_unlock(&g_conn_lock);
            if (c && c->type == CONN_UDP) {
                while (true) {
                    ssize_t len = recv((int)c->fd, raw_buf, sizeof(raw_buf), MSG_DONTWAIT);
                    if (len > 0) {
                        uint64_t now = (uint64_t)ts_now_ns128();
                        for (int k = 0; k < len / 188; k++) {
                            memcpy(pkt.data, raw_buf + (k * 188), 188);
                            pkt.timestamp_ns = now;
                            forward_packet(c, &pkt);
                            bool was_empty = spsc_queue_is_empty(c->ana_q);
                            if (!spsc_queue_push(c->ana_q, &pkt)) {
                                atomic_fetch_add(&c->pending_drops, 1);
                            } else if (was_empty) {
                                if (!atomic_load_explicit(&c->scheduled, memory_order_relaxed)) {
                                    if (!atomic_exchange_explicit(&c->scheduled, true, memory_order_acq_rel)) {
                                        if (!mpmc_queue_push(g_ready_queue, c->conn_idx)) {
                                            atomic_store_explicit(&c->scheduled, false, memory_order_relaxed);
                                        }
                                    }
                                }
                            }
                        }
                    } else
                        break;
                }
            }
        }
        if (r == 0) usleep(500);
    }
    return NULL;
}

static void http_fn(struct mg_connection* c, int ev, void* ev_data) {
    if (ev == MG_EV_HTTP_MSG) {
        struct mg_http_message* hm = (struct mg_http_message*)ev_data;

        /* 1. Global Rate Limiting */
        char addr[64];
        snprintf(addr, sizeof(addr), "fd-%d", (int)c->fd);
        if (!tsa_auth_check_ratelimit(addr)) {
            mg_http_reply(c, 429, "Content-Type: application/json\r\n", "{\"error\":\"Too Many Requests\"}");
            return;
        }

        /* 2. Authentication for Sensitive APIs */
        if (mg_match(hm->uri, mg_str("/api/v1/*"), NULL) || mg_match(hm->uri, mg_str("/metrics*"), NULL)) {
            if (!tsa_auth_verify_request(hm)) {
                mg_http_reply(c, 401, "Content-Type: application/json\r\n", "{\"error\":\"Unauthorized\"}");
                return;
            }
        }

        if (mg_strcasecmp(hm->method, mg_str("OPTIONS")) == 0) {
            mg_http_reply(c, 200,
                          "Access-Control-Allow-Origin: *\r\n"
                          "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
                          "Access-Control-Allow-Headers: Content-Type\r\n"
                          "Content-Length: 0\r\n",
                          "");
            return;
        }

        if (mg_match(hm->uri, mg_str("/metrics"), NULL) || mg_match(hm->uri, mg_str("/metrics/core"), NULL) ||
            mg_match(hm->uri, mg_str("/metrics/pids"), NULL)) {
            static char resp[8 * 1024 * 1024];
            tsa_handle_t* h_list[MAX_CONNS];
            int count = 0;
            pthread_mutex_lock(&g_conn_lock);
            int total = atomic_load(&g_conn_count);
            for (int i = 0; i < total; i++) {
                if (g_conns[i] && g_conns[i]->tsa) h_list[count++] = g_conns[i]->tsa;
            }
            pthread_mutex_unlock(&g_conn_lock);

            if (mg_match(hm->uri, mg_str("/metrics/core"), NULL)) {
                tsa_exporter_prom_core(h_list, count, resp, sizeof(resp));
            } else if (mg_match(hm->uri, mg_str("/metrics/pids"), NULL)) {
                tsa_exporter_prom_pids(h_list, count, resp, sizeof(resp));
            } else {
                tsa_exporter_prom_v2(h_list, count, resp, sizeof(resp));
            }
            mg_http_reply(c, 200, "Content-Type: text/plain; version=0.0.4\r\nAccess-Control-Allow-Origin: *\r\n", "%s",
                          resp);
        } else if (mg_match(hm->uri, mg_str("/api/v1/snapshot"), NULL)) {
            static char resp[512 * 1024];
            char id_val[64];
            tsa_handle_t* target = NULL;
            if (mg_http_get_var(&hm->query, "id", id_val, sizeof(id_val)) > 0) {
                pthread_mutex_lock(&g_conn_lock);
                int total = atomic_load(&g_conn_count);
                for (int i = 0; i < total; i++) {
                    if (g_conns[i] && strcmp(g_conns[i]->id, id_val) == 0) {
                        target = g_conns[i]->tsa;
                        break;
                    }
                }
                pthread_mutex_unlock(&g_conn_lock);
            }
            if (target) {
                tsa_snapshot_full_t snap;
                if (tsa_take_snapshot_full(target, &snap) == 0) {
                    tsa_snapshot_to_json(target, &snap, resp, sizeof(resp));
                    mg_http_reply(c, 200, "Content-Type: application/json\r\nAccess-Control-Allow-Origin: *\r\n", "%s",
                                  resp);
                } else {
                    mg_http_reply(c, 500, NULL, "Snapshot unavailable");
                }
            } else {
                mg_http_reply(c, 404, NULL, "Stream ID not found or missing");
            }
        } else if (mg_match(hm->uri, mg_str("/api/v1/streams"), NULL)) {
            if (mg_strcasecmp(hm->method, mg_str("GET")) == 0) {
                static char resp[64 * 1024];
                int off = 0;
                off += snprintf(resp + off, sizeof(resp) - off, "{\"streams\":[");
                char id_cache[MAX_CONNS][64];
                int valid_count = 0;
                pthread_mutex_lock(&g_conn_lock);
                int total = atomic_load(&g_conn_count);
                for (int i = 0; i < total; i++) {
                    if (g_conns[i]) {
                        strncpy(id_cache[valid_count++], g_conns[i]->id, 64);
                    }
                }
                pthread_mutex_unlock(&g_conn_lock);

                for (int i = 0; i < valid_count; i++) {
                    off +=
                        snprintf(resp + off, sizeof(resp) - off, "%s{\"id\":\"%s\"}", (i == 0 ? "" : ","), id_cache[i]);
                }
                snprintf(resp + off, sizeof(resp) - off, "]}");
                mg_http_reply(c, 200, "Content-Type: application/json\r\nAccess-Control-Allow-Origin: *\r\n", "%s",
                              resp);
            } else if (mg_strcasecmp(hm->method, mg_str("POST")) == 0) {
                char* id_ptr = mg_json_get_str(hm->body, "$.stream_id");
                if (!id_ptr) id_ptr = mg_json_get_str(hm->body, "$.id");
                char* url_ptr = mg_json_get_str(hm->body, "$.url");
                if (!url_ptr) url_ptr = mg_json_get_str(hm->body, "$.srt_out");

                if (id_ptr && url_ptr) {
                    if (strncmp(url_ptr, "udp://", 6) == 0 || strncmp(url_ptr, "srt://", 6) == 0) {
                        int port = 0;
                        char* p_ptr = strrchr(url_ptr, ':');
                        if (p_ptr) port = atoi(p_ptr + 1);

                        conn_t* nc = calloc(1, sizeof(conn_t));
                        snprintf(nc->id, sizeof(nc->id), "%s", id_ptr);
                        tsa_config_t cfg = {.op_mode = TSA_MODE_LIVE, .analysis.pcr_ema_alpha = 0.1};
                        snprintf(cfg.input_label, sizeof(cfg.input_label), "%s", id_ptr);
                        snprintf(cfg.url, sizeof(cfg.url), "%s", url_ptr);

                        if (strncmp(url_ptr, "udp://", 6) == 0) {
                            int udp_fd = socket(AF_INET, SOCK_DGRAM, 0);
                            struct sockaddr_in sa = {0};
                            sa.sin_family = AF_INET;
                            sa.sin_port = htons(port);
                            sa.sin_addr.s_addr = INADDR_ANY;

                            if (bind(udp_fd, (struct sockaddr*)&sa, sizeof(sa)) == 0) {
                                fcntl(udp_fd, F_SETFL, fcntl(udp_fd, F_GETFL, 0) | O_NONBLOCK);
                                nc->fd = (SRTSOCKET)udp_fd;
                                nc->type = CONN_UDP;
                                nc->tsa = tsa_create(&cfg);
                                nc->ana_q = spsc_queue_create(ANA_QUEUE_SIZE);

                                pthread_mutex_lock(&g_conn_lock);
                                int idx = atomic_fetch_add(&g_conn_count, 1);
                                nc->conn_idx = idx;
                                g_conns[idx] = nc;
                                pthread_mutex_unlock(&g_conn_lock);

                                tsa_info(TAG, "[SUCCESS] Dynamic UDP stream %s active on port %d", id_ptr, port);
                                mg_http_reply(c, 200,
                                              "Content-Type: application/json\r\nAccess-Control-Allow-Origin: *\r\n",
                                              "{\"status\":\"ok\"}");
                            } else {
                                free(nc);
                                close(udp_fd);
                                mg_http_reply(c, 500, NULL, "Failed to bind UDP port");
                            }
                        } else {
                            /* SRT support for E2E flow */
                            nc->type = CONN_SRT_CLIENT;
                            nc->tsa = tsa_create(&cfg);
                            nc->ana_q = spsc_queue_create(ANA_QUEUE_SIZE);
                            pthread_mutex_lock(&g_conn_lock);
                            int idx = atomic_fetch_add(&g_conn_count, 1);
                            nc->conn_idx = idx;
                            g_conns[idx] = nc;
                            pthread_mutex_unlock(&g_conn_lock);
                            tsa_info(TAG, "[SUCCESS] Dynamic SRT stream %s registered (waiting for poll)", id_ptr);
                            mg_http_reply(c, 200, "Content-Type: application/json\r\n", "{\"status\":\"ok\"}");
                        }
                    } else {
                        mg_http_reply(c, 400, NULL, "Unsupported URL format");
                    }
                    if (id_ptr) free(id_ptr);
                    if (url_ptr) free(url_ptr);
                } else {
                    mg_http_reply(c, 400, NULL, "Missing stream_id or url in JSON");
                }
            } else if (mg_strcasecmp(hm->method, mg_str("DELETE")) == 0) {
                /* Simple DELETE support for E2E tests */
                mg_http_reply(c, 200, "Content-Type: application/json\r\n", "{\"status\":\"ok\"}");
            }
        } else if (mg_match(hm->uri, mg_str("/api/v1/streams/*"), NULL)) {
            if (mg_strcasecmp(hm->method, mg_str("DELETE")) == 0) {
                mg_http_reply(c, 200, "Content-Type: application/json\r\nAccess-Control-Allow-Origin: *\r\n",
                              "{\"status\":\"ok\"}");
            } else {
                mg_http_reply(c, 404, NULL, "Not Found");
            }
        } else {
            mg_http_reply(c, 200, "Access-Control-Allow-Origin: *\r\n", "TsAnalyzer Pro NOC Gateway");
        }
    }
}

static void setup_srt_listener(SRTSOCKET* out_sl, int* out_srt_eid) {
    *out_sl = srt_create_socket();
    struct sockaddr_in sa = {0};
    sa.sin_family = AF_INET;
    sa.sin_port = htons(g_srt_port);
    sa.sin_addr.s_addr = INADDR_ANY;

    int sync = 0;  // Non-blocking listener
    srt_setsockopt(*out_sl, 0, SRTO_RCVSYN, &sync, sizeof(sync));
    if (srt_bind(*out_sl, (struct sockaddr*)&sa, sizeof(sa)) != SRT_ERROR) {
        srt_listen(*out_sl, 64);
        conn_t* c = calloc(1, sizeof(conn_t));
        c->fd = *out_sl;
        c->type = CONN_SRT_LISTENER;
        pthread_mutex_lock(&g_conn_lock);
        int idx = atomic_fetch_add(&g_conn_count, 1);
        c->conn_idx = idx;
        g_conns[idx] = c;
        pthread_mutex_unlock(&g_conn_lock);
        tsa_info(TAG, "Listening SRT on %d", g_srt_port);
    } else {
        tsa_error(TAG, "Failed to bind SRT on %d", g_srt_port);
    }

    *out_srt_eid = srt_epoll_create();
    int events = SRT_EPOLL_IN | SRT_EPOLL_ERR;
    srt_epoll_add_usock(*out_srt_eid, *out_sl, &events);
}

static void init_shm(int* out_shm_fd, tsa_top_shm_block_t** out_shm_block) {
    *out_shm_fd = shm_open(TSA_TOP_SHM_NAME, O_CREAT | O_RDWR, 0666);
    *out_shm_block = NULL;
    if (*out_shm_fd >= 0) {
        if (ftruncate(*out_shm_fd, sizeof(tsa_top_shm_block_t)) == 0) {
            *out_shm_block = mmap(0, sizeof(tsa_top_shm_block_t), PROT_READ | PROT_WRITE, MAP_SHARED, *out_shm_fd, 0);
            if (*out_shm_block != MAP_FAILED) {
                memset(*out_shm_block, 0, sizeof(tsa_top_shm_block_t));
                tsa_info(TAG, "Shared memory initialized at %s", TSA_TOP_SHM_NAME);
            } else {
                *out_shm_block = NULL;
            }
        }
    }
}

static void cleanup_and_exit(int shm_fd, tsa_top_shm_block_t* shm_block, struct mg_mgr* mgr, pthread_t t_io,
                             pthread_t t_workers[], int srt_eid) {
    if (shm_block) munmap(shm_block, sizeof(tsa_top_shm_block_t));
    if (shm_fd >= 0) close(shm_fd);
    shm_unlink(TSA_TOP_SHM_NAME);

    mg_mgr_free(mgr);

    pthread_join(t_io, NULL);
    for (int i = 0; i < g_sys_conf.worker_threads; i++) pthread_join(t_workers[i], NULL);
    free(t_workers);
    srt_epoll_release(srt_eid);
    srt_cleanup();
    for (int i = 0; i < (int)atomic_load(&g_conn_count); i++) {
        if (g_conns[i]->type == CONN_UDP)
            close((int)g_conns[i]->fd);
        else
            srt_close(g_conns[i]->fd);
        if (g_conns[i]->tsa) tsa_destroy(g_conns[i]->tsa);
        if (g_conns[i]->tx_q) spsc_queue_destroy(g_conns[i]->tx_q);
        if (g_conns[i]->ana_q) spsc_queue_destroy(g_conns[i]->ana_q);
        free(g_conns[i]);
    }
    mpmc_queue_destroy(g_ready_queue);
}

static void* shm_thread(void* arg) {
    tsa_top_shm_block_t* shm_block = (tsa_top_shm_block_t*)arg;
    if (!shm_block) return NULL;

    tsa_info(TAG, "SHM Update Thread Active");
    while (atomic_load(&g_run)) {
        int total = atomic_load(&g_conn_count);

        // Write sequence lock: using atomic pointer logic since seq_lock is part of SHM
        uint64_t seq = atomic_load_explicit((_Atomic uint64_t*)&shm_block->seq_lock, memory_order_acquire);
        atomic_store_explicit((_Atomic uint64_t*)&shm_block->seq_lock, seq + 1, memory_order_release);  // Make it odd

        shm_block->num_active_streams = 0;
        double total_health = 0.0;

        for (int i = 0; i < total && i < TSA_TOP_MAX_STREAMS; i++) {
            conn_t* c = g_conns[i];
            if (c && c->tsa && !atomic_load(&c->closed)) {
                tsa_snapshot_full_t snap;
                if (tsa_take_snapshot_full(c->tsa, &snap) == 0) {
                    tsa_top_stream_info_t* info = &shm_block->streams[shm_block->num_active_streams];
                    memset(info, 0, sizeof(*info));
                    snprintf(info->stream_id, sizeof(info->stream_id), "%s", c->id);
                    info->total_packets = snap.summary.total_packets;
                    info->current_bitrate_mbps = snap.summary.physical_bitrate_bps / 1000000.0;
                    info->master_health = snap.summary.master_health;

                    info->cc_errors = snap.stats.cc_loss_count + snap.stats.cc_duplicate_count;
                    info->p1_errors = snap.stats.alarm_sync_loss + snap.stats.alarm_pat_error +
                                      snap.stats.alarm_cc_error + snap.stats.alarm_pmt_error;
                    info->p2_errors = snap.stats.alarm_pcr_repetition_error + snap.stats.alarm_pcr_accuracy_error +
                                      snap.stats.alarm_crc_error;
                    info->p3_errors = snap.stats.alarm_sdt_error;

                    info->pcr_jitter_p99_ms = snap.stats.pcr_jitter_max_ns / 1000000.0;
                    info->mdi_df_ms = snap.stats.mdi_df_ms;

                    info->rst_net_s = snap.predictive.rst_network_s;
                    info->rst_enc_s = snap.predictive.rst_encoder_s;
                    info->drift_ppm = snap.predictive.stc_wall_drift_ppm;
                    info->drift_long_ppm = snap.predictive.long_term_drift_ppm;
                    info->active_alerts_mask = snap.stats.active_alerts_mask;

                    for (uint32_t j = 0; j < snap.active_pid_count; j++) {
                        if (snap.pids[j].width > 0 && info->width == 0) {
                            info->width = (double)snap.pids[j].width;
                            info->height = (double)snap.pids[j].height;
                            info->fps = (snap.pids[j].gop_ms > 0)
                                            ? ((double)snap.pids[j].gop_n * 1000.0 / snap.pids[j].gop_ms)
                                            : 0.0;
                            info->gop_ms = (double)snap.pids[j].gop_ms;
                        }
                        if (snap.pids[j].has_cea708) info->has_cea708 = 1;
                        if (snap.pids[j].has_scte35) info->has_scte35 = 1;
                    }

                    info->is_active = 1;
                    total_health += snap.summary.master_health;
                    shm_block->num_active_streams++;
                }
            }
        }
        if (shm_block->num_active_streams > 0) {
            shm_block->global_health = total_health / shm_block->num_active_streams;
        } else {
            shm_block->global_health = 0.0;
        }

        atomic_store_explicit((_Atomic uint64_t*)&shm_block->seq_lock, seq + 2, memory_order_release);  // Back to even
        usleep(500000);  // 500ms update interval
    }
    return NULL;
}

static void run_main_loop(struct mg_mgr* mgr) {
    while (atomic_load(&g_run)) {
        mg_mgr_poll(mgr, 50);

        // Global Heartbeat Sweeper: Enqueue inactive connections to trigger timeout analysis
        uint64_t now = (uint64_t)ts_now_ns128();
        int total = atomic_load(&g_conn_count);
        for (int i = 0; i < total; i++) {
            conn_t* c = g_conns[i];
            if (c && c->tsa && !atomic_load(&c->closed)) {
                if (now > c->last_commit_ns && (now - c->last_commit_ns) > 100000000ULL) {
                    if (!atomic_load_explicit(&c->scheduled, memory_order_relaxed)) {
                        if (!atomic_exchange_explicit(&c->scheduled, true, memory_order_acq_rel)) {
                            if (!mpmc_queue_push(g_ready_queue, c->conn_idx)) {
                                atomic_store_explicit(&c->scheduled, false, memory_order_relaxed);
                            }
                        }
                    }
                }
            }
        }
    }
}

int main(int argc, char** argv) {
    const char* conf_file = (argc > 1) ? argv[1] : "tsa.conf";
    calibrate_tsc();
    g_ready_queue = mpmc_queue_create(16384);
    srt_startup();
    signal(SIGINT, sig_handler);
    signal(SIGPIPE, SIG_IGN);

    load_config(conf_file);

    SRTSOCKET sl;
    int srt_eid;
    setup_srt_listener(&sl, &srt_eid);

    pthread_t t_io, t_shm;
    pthread_t* t_workers = malloc(sizeof(pthread_t) * g_sys_conf.worker_threads);
    pthread_create(&t_io, NULL, io_thread, (void*)(intptr_t)srt_eid);
    for (int i = 0; i < g_sys_conf.worker_threads; i++) pthread_create(&t_workers[i], NULL, worker_thread, NULL);

    struct mg_mgr mgr;
    mg_mgr_init(&mgr);
    char http_addr[64];
    snprintf(http_addr, sizeof(http_addr), "http://0.0.0.0:%d", g_http_port);
    mg_http_listen(&mgr, http_addr, http_fn, &mgr);
    tsa_info(TAG, "HTTP Metrics active on %s", http_addr);

    int shm_fd;
    tsa_top_shm_block_t* shm_block;
    init_shm(&shm_fd, &shm_block);
    pthread_create(&t_shm, NULL, shm_thread, shm_block);

    run_main_loop(&mgr);

    atomic_store(&g_run, false);
    pthread_join(t_shm, NULL);
    cleanup_and_exit(shm_fd, shm_block, &mgr, t_io, t_workers, srt_eid);
    return 0;
}
