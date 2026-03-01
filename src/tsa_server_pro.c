#define _GNU_SOURCE
#include <arpa/inet.h>
#include <errno.h>
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
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include "../deps/mongoose/mongoose.h"
#include "spsc_queue.h"
#include "tsa.h"
#include "tsa_internal.h"

#define MAX_CONNS 1024
#define HTTP_PORT "8080"
#define PACKET_BUF_SIZE 65536
#define WORKER_THREADS 8

typedef enum { CONN_UDP, CONN_SRT_LISTENER, CONN_SRT_CLIENT } conn_type_t;

typedef struct {
    SRTSOCKET fd;
    conn_type_t type;
    char id[64];
    tsa_handle_t* tsa;
    spsc_queue_t* q;
    atomic_bool processing;
    atomic_bool closed;
} conn_t;

static conn_t* g_conns[MAX_CONNS];
static atomic_int g_conn_count = 0;
static _Atomic bool g_run = true;
static pthread_mutex_t g_conn_lock = PTHREAD_MUTEX_INITIALIZER;

extern void tsa_exporter_prom_v2(tsa_handle_t** handles, int count, char* buf, size_t sz);

static void sig_handler(int sig) {
    (void)sig;
    atomic_store(&g_run, false);
}

static void* worker_thread(void* arg) {
    (void)arg;
    ts_packet_t pkt;
    while (atomic_load(&g_run)) {
        bool idle = true;
        int count = atomic_load(&g_conn_count);
        for (int i = 0; i < count; i++) {
            pthread_mutex_lock(&g_conn_lock);
            conn_t* c = g_conns[i];
            pthread_mutex_unlock(&g_conn_lock);
            
            if (!c || !c->tsa || !c->q || atomic_load(&c->closed)) continue;

            bool expected = false;
            if (atomic_compare_exchange_weak(&c->processing, &expected, true)) {
                int drained = 0;
                uint64_t now = (uint64_t)ts_now_ns128();
                while (spsc_queue_pop(c->q, &pkt)) {
                    tsa_process_packet(c->tsa, pkt.data, pkt.timestamp_ns);
                    drained++;
                    if (drained > 2000) break;
                }
                if (drained > 0) {
                    tsa_commit_snapshot(c->tsa, now);
                    idle = false;
                }
                atomic_store(&c->processing, false);
            }
        }
        if (idle) usleep(500);
    }
    return NULL;
}

static void* io_thread(void* arg) {
    int srt_eid = (int)(intptr_t)arg;
    uint8_t raw_buf[4096];
    ts_packet_t pkt;

    printf("PRO: I/O Thread Active (SRT eid=%d)\n", srt_eid);

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
                        printf("PRO: Accepted SRT fd=%d\n", (int)client);
                        int sync = 0;
                        srt_setsockopt(client, 0, SRTO_RCVSYN, &sync, sizeof(sync));
                        srt_setsockopt(client, 0, SRTO_SNDSYN, &sync, sizeof(sync));

                        conn_t* nc = calloc(1, sizeof(conn_t));
                        nc->fd = client;
                        nc->type = CONN_SRT_CLIENT;
                        sprintf(nc->id, "SRT-%d", (int)client);
                        tsa_config_t cfg = {.is_live = true, .pcr_ema_alpha = 0.1};
                        strncpy(cfg.input_label, nc->id, 31);
                        nc->tsa = tsa_create(&cfg);
                        nc->q = spsc_queue_create(PACKET_BUF_SIZE);
                        int events = SRT_EPOLL_IN | SRT_EPOLL_ERR;
                        srt_epoll_add_usock(srt_eid, client, &events);
                        
                        pthread_mutex_lock(&g_conn_lock);
                        g_conns[atomic_fetch_add(&g_conn_count, 1)] = nc;
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
                                spsc_queue_push(c->q, &pkt);
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
                                printf("PRO: Closed SRT fd=%d (%s)\n", (int)s, srt_getlasterror_str());
                                srt_epoll_remove_usock(srt_eid, s);
                                atomic_store(&c->closed, true);
                            }
                            break;
                        } else break;
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
                    ssize_t len = recv(c->fd, raw_buf, sizeof(raw_buf), MSG_DONTWAIT);
                    if (len > 0) {
                        uint64_t now = (uint64_t)ts_now_ns128();
                        for (int k = 0; k < len / 188; k++) {
                            memcpy(pkt.data, raw_buf + (k * 188), 188);
                            pkt.timestamp_ns = now;
                            spsc_queue_push(c->q, &pkt);
                        }
                    } else break;
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
        if (mg_match(hm->uri, mg_str("/metrics"), NULL)) {
            static char resp[4 * 1024 * 1024];
            tsa_handle_t* h_list[MAX_CONNS];
            int count = 0;
            pthread_mutex_lock(&g_conn_lock);
            int total = atomic_load(&g_conn_count);
            for (int i = 0; i < total; i++) {
                if (g_conns[i]->tsa) h_list[count++] = g_conns[i]->tsa;
            }
            pthread_mutex_unlock(&g_conn_lock);
            tsa_exporter_prom_v2(h_list, count, resp, sizeof(resp));
            mg_http_reply(c, 200, "Content-Type: text/plain\r\nAccess-Control-Allow-Origin: *\r\n", "%s", resp);
        } else {
            mg_http_reply(c, 200, "", "TsAnalyzer Pro - SRS Pattern Concurrent Server\n");
        }
    }
}

int main(int argc, char** argv) {
    int port = (argc > 1) ? atoi(argv[1]) : 9000;
    srt_startup();
    signal(SIGINT, sig_handler);
    signal(SIGPIPE, SIG_IGN);

    SRTSOCKET sl = srt_create_socket();
    struct sockaddr_in sa = {0};
    sa.sin_family = AF_INET;
    sa.sin_port = htons(port);
    sa.sin_addr.s_addr = INADDR_ANY;
    
    int sync = 1;
    srt_setsockopt(sl, 0, SRTO_RCVSYN, &sync, sizeof(sync));
    if (srt_bind(sl, (struct sockaddr*)&sa, sizeof(sa)) != SRT_ERROR) {
        srt_listen(sl, 64);
        conn_t* c = calloc(1, sizeof(conn_t));
        c->fd = sl;
        c->type = CONN_SRT_LISTENER;
        g_conns[atomic_fetch_add(&g_conn_count, 1)] = c;
        printf("PRO: Listening SRT on %d\n", port);
    }

    int udp_fd = socket(AF_INET, SOCK_DGRAM, 0);
    sa.sin_port = htons(19001);
    if (bind(udp_fd, (struct sockaddr*)&sa, sizeof(sa)) == 0) {
        conn_t* c = calloc(1, sizeof(conn_t));
        c->fd = udp_fd;
        c->type = CONN_UDP;
        sprintf(c->id, "UDP-19001");
        tsa_config_t cfg = {.is_live = true, .pcr_ema_alpha = 0.1};
        strncpy(cfg.input_label, c->id, 31);
        c->tsa = tsa_create(&cfg);
        c->q = spsc_queue_create(PACKET_BUF_SIZE);
        g_conns[atomic_fetch_add(&g_conn_count, 1)] = c;
        printf("PRO: Listening UDP on 19001\n");
    }

    int srt_eid = srt_epoll_create();
    int events = SRT_EPOLL_IN | SRT_EPOLL_ERR;
    srt_epoll_add_usock(srt_eid, sl, &events);

    pthread_t t_io, t_workers[WORKER_THREADS];
    pthread_create(&t_io, NULL, io_thread, (void*)(intptr_t)srt_eid);
    for (int i = 0; i < WORKER_THREADS; i++) pthread_create(&t_workers[i], NULL, worker_thread, NULL);

    struct mg_mgr mgr;
    mg_mgr_init(&mgr);
    mg_http_listen(&mgr, "http://0.0.0.0:" HTTP_PORT, http_fn, NULL);
    
    while (atomic_load(&g_run)) mg_mgr_poll(&mgr, 50);

    mg_mgr_free(&mgr);
    pthread_join(t_io, NULL);
    for (int i = 0; i < WORKER_THREADS; i++) pthread_join(t_workers[i], NULL);
    
    srt_epoll_release(srt_eid);
    srt_cleanup();
    return 0;
}
