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
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>

#include "mongoose.h"
#include "mpmc_queue.h"
#include "spsc_queue.h"
#include "tsa.h"
#include "tsa_internal.h"
#include "../include/tsa_top_shm.h"

#define MAX_CONNS 4096
#define HTTP_PORT "8081"
#define PACKET_BUF_SIZE 65536
#define WORKER_THREADS 16
#define ANA_QUEUE_SIZE 512

typedef enum { CONN_UDP, CONN_SRT_LISTENER, CONN_SRT_CLIENT } conn_type_t;

typedef struct {
    SRTSOCKET fd;
    int tx_fd; /* Forwarding socket */
    conn_type_t type;
    char id[64];
    tsa_handle_t* tsa;
    spsc_queue_t* tx_q;  /* Forwarding Queue */
    spsc_queue_t* ana_q; /* Analysis Queue */
    _Atomic bool scheduled;
    atomic_bool closed;
    _Atomic uint64_t pending_drops;
    uint32_t conn_idx;
    uint64_t last_commit_ns; // Heartbeat tracking
} conn_t;

static conn_t* g_conns[MAX_CONNS];
static atomic_int g_conn_count = 0;
static _Atomic bool g_run = true;
static pthread_mutex_t g_conn_lock = PTHREAD_MUTEX_INITIALIZER;
static mpmc_queue_t* g_ready_queue;

static int g_http_port = 8081;
static int g_srt_port = 9000;

extern void tsa_exporter_prom_v2(tsa_handle_t** handles, int count, char* buf, size_t sz);
extern void tsa_exporter_prom_core(tsa_handle_t** handles, int count, char* buf, size_t sz);
extern void tsa_exporter_prom_pids(tsa_handle_t** handles, int count, char* buf, size_t sz);

static void load_config(const char* file) {
    FILE* fp = fopen(file, "r");
    if (!fp) return;
    char line[512], id[64], url[256];
    struct sockaddr_in sa = {0};
    sa.sin_family = AF_INET;
    sa.sin_addr.s_addr = INADDR_ANY;

    while (fgets(line, sizeof(line), fp) && atomic_load(&g_conn_count) < MAX_CONNS) {
        if (line[0] == '#' || line[0] == '\n') continue;
        if (strncmp(line, "GLOBAL", 6) == 0) {
            char key[32];
            int val;
            if (sscanf(line + 7, "%s %d", key, &val) == 2) {
                if (strcmp(key, "http_port") == 0)
                    g_http_port = val;
                else if (strcmp(key, "srt_port") == 0)
                    g_srt_port = val;
            }
            continue;
        }
        if (sscanf(line, "%s %s", id, url) == 2) {
            conn_t* c = calloc(1, sizeof(conn_t));
            strncpy(c->id, id, 63);
            tsa_config_t cfg = {.is_live = true, .pcr_ema_alpha = 0.1};
            strncpy(cfg.input_label, id, 31);

            if (strncmp(url, "udp://", 6) == 0) {
                char* p_str = strrchr(url, ':');
                int port = p_str ? atoi(p_str + 1) : 19001;
                int udp_fd = socket(AF_INET, SOCK_DGRAM, 0);
                sa.sin_port = htons(port);
                if (bind(udp_fd, (struct sockaddr*)&sa, sizeof(sa)) == 0) {
                    int flags = fcntl(udp_fd, F_GETFL, 0);
                    fcntl(udp_fd, F_SETFL, flags | O_NONBLOCK);
                    c->fd = (SRTSOCKET)udp_fd;
                    c->type = CONN_UDP;
                    c->tsa = tsa_create(&cfg);
                    c->tx_q = spsc_queue_create(1024);
                    c->ana_q = spsc_queue_create(ANA_QUEUE_SIZE);
                    atomic_init(&c->scheduled, false);
                    pthread_mutex_lock(&g_conn_lock);
                    int idx = atomic_fetch_add(&g_conn_count, 1);
                    c->conn_idx = idx;
                    g_conns[idx] = c;
                    pthread_mutex_unlock(&g_conn_lock);
                    printf("PRO: Configured UDP stream %s on port %d\n", id, port);
                } else {
                    free(c);
                    close(udp_fd);
                }
            }
            /* SRT Caller or other types can be added here */
        }
    }
    fclose(fp);
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
    uint64_t slice_cycles = 500 * g_cycles_per_us;  // 500us quota

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

                if (drained > 0 || (now > c->last_commit_ns && (now - c->last_commit_ns) > 100000000ULL)) {
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
                                printf("PRO: Closed SRT fd=%d (%s)\n", (int)s, srt_getlasterror_str());
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
        if (mg_match(hm->uri, mg_str("/metrics"), NULL) || mg_match(hm->uri, mg_str("/metrics/core"), NULL) ||
            mg_match(hm->uri, mg_str("/metrics/pids"), NULL)) {
            static char resp[4 * 1024 * 1024];
            tsa_handle_t* h_list[MAX_CONNS];
            int count = 0;
            pthread_mutex_lock(&g_conn_lock);
            int total = atomic_load(&g_conn_count);
            for (int i = 0; i < total; i++) {
                if (g_conns[i]->tsa) h_list[count++] = g_conns[i]->tsa;
            }
            pthread_mutex_unlock(&g_conn_lock);

            if (mg_match(hm->uri, mg_str("/metrics/core"), NULL)) {
                tsa_exporter_prom_core(h_list, count, resp, sizeof(resp));
            } else if (mg_match(hm->uri, mg_str("/metrics/pids"), NULL)) {
                tsa_exporter_prom_pids(h_list, count, resp, sizeof(resp));
            } else {
                tsa_exporter_prom_v2(h_list, count, resp, sizeof(resp));
            }
            mg_http_reply(c, 200, "Content-Type: text/plain\r\nAccess-Control-Allow-Origin: *\r\n", "%s", resp);
        } else if (mg_match(hm->uri, mg_str("/api/v1/snapshot"), NULL)) {
            static char resp[512 * 1024];
            char id_val[64];
            tsa_handle_t* target = NULL;
            if (mg_http_get_var(&hm->query, "id", id_val, sizeof(id_val)) > 0) {
                pthread_mutex_lock(&g_conn_lock);
                int total = atomic_load(&g_conn_count);
                for (int i = 0; i < total; i++) {
                    if (strcmp(g_conns[i]->id, id_val) == 0) {
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
                    mg_http_reply(c, 500, NULL, "Snapshot unavailable\n");
                }
            } else {
                mg_http_reply(c, 404, NULL, "Stream ID not found or missing\n");
            }
        } else {
            mg_http_reply(c, 200, "", "TsAnalyzer Pro - Appliance Metric Gateway\n");
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

    /* Setup SRT Listener using configured port */
    SRTSOCKET sl = srt_create_socket();
    struct sockaddr_in sa = {0};
    sa.sin_family = AF_INET;
    sa.sin_port = htons(g_srt_port);
    sa.sin_addr.s_addr = INADDR_ANY;

    int sync = 0;  // Non-blocking listener
    srt_setsockopt(sl, 0, SRTO_RCVSYN, &sync, sizeof(sync));
    if (srt_bind(sl, (struct sockaddr*)&sa, sizeof(sa)) != SRT_ERROR) {
        srt_listen(sl, 64);
        conn_t* c = calloc(1, sizeof(conn_t));
        c->fd = sl;
        c->type = CONN_SRT_LISTENER;
        pthread_mutex_lock(&g_conn_lock);
        int idx = atomic_fetch_add(&g_conn_count, 1);
        c->conn_idx = idx;
        g_conns[idx] = c;
        pthread_mutex_unlock(&g_conn_lock);
        printf("PRO: Listening SRT on %d\n", g_srt_port);
    } else {
        fprintf(stderr, "PRO: Failed to bind SRT on %d\n", g_srt_port);
    }

    int srt_eid = srt_epoll_create();
    int events = SRT_EPOLL_IN | SRT_EPOLL_ERR;
    srt_epoll_add_usock(srt_eid, sl, &events);

    pthread_t t_io, t_workers[WORKER_THREADS];
    pthread_create(&t_io, NULL, io_thread, (void*)(intptr_t)srt_eid);
    for (int i = 0; i < WORKER_THREADS; i++) pthread_create(&t_workers[i], NULL, worker_thread, NULL);

    struct mg_mgr mgr;
    mg_mgr_init(&mgr);
    char http_addr[64];
    snprintf(http_addr, sizeof(http_addr), "http://0.0.0.0:%d", g_http_port);
    mg_http_listen(&mgr, http_addr, http_fn, &mgr);
    printf("PRO: HTTP Metrics active on %s\n", http_addr);

    // Initialize Shared Memory for tsa_top
    int shm_fd = shm_open(TSA_TOP_SHM_NAME, O_CREAT | O_RDWR, 0666);
    tsa_top_shm_block_t* shm_block = NULL;
    if (shm_fd >= 0) {
        if (ftruncate(shm_fd, sizeof(tsa_top_shm_block_t)) == 0) {
            shm_block = mmap(0, sizeof(tsa_top_shm_block_t), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
            if (shm_block != MAP_FAILED) {
                memset(shm_block, 0, sizeof(tsa_top_shm_block_t));
                printf("PRO: Shared memory initialized at %s\n", TSA_TOP_SHM_NAME);
            } else {
                shm_block = NULL;
            }
        }
    }

    uint64_t last_shm_update = 0;

    while (atomic_load(&g_run)) {
        mg_mgr_poll(&mgr, 50);
        
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

        // Update Shared Memory (every ~500ms) with Sequence Locking
        if (shm_block && (now - last_shm_update) > 500000000ULL) {
            // Write sequence lock: using atomic pointer logic since seq_lock is part of SHM
            uint64_t seq = atomic_load_explicit((_Atomic uint64_t*)&shm_block->seq_lock, memory_order_acquire);
            atomic_store_explicit((_Atomic uint64_t*)&shm_block->seq_lock, seq + 1, memory_order_release); // Make it odd

            shm_block->num_active_streams = 0;
            double total_health = 0.0;

            for (int i = 0; i < total && i < TSA_TOP_MAX_STREAMS; i++) {
                conn_t* c = g_conns[i];
                if (c && c->tsa && !atomic_load(&c->closed)) {
                    tsa_snapshot_full_t snap;
                    if (tsa_take_snapshot_full(c->tsa, &snap) == 0) {
                        tsa_top_stream_info_t* info = &shm_block->streams[shm_block->num_active_streams];
                        memset(info, 0, sizeof(*info));
                        strncpy(info->stream_id, c->id, sizeof(info->stream_id) - 1);
                        info->total_packets = snap.summary.total_packets;
                        info->current_bitrate_mbps = snap.summary.physical_bitrate_bps / 1000000.0;
                        info->master_health = snap.summary.master_health;
                        
                        info->cc_errors = snap.stats.cc_loss_count + snap.stats.cc_duplicate_count;
                        info->p1_errors = snap.stats.alarm_sync_loss + snap.stats.alarm_pat_error + snap.stats.alarm_cc_error + snap.stats.alarm_pmt_error;
                        info->p2_errors = snap.stats.alarm_pcr_repetition_error + snap.stats.alarm_pcr_accuracy_error + snap.stats.alarm_crc_error;
                        info->p3_errors = snap.stats.alarm_sdt_error;
                        
                        info->pcr_jitter_p99_ms = snap.stats.pcr_jitter_max_ns / 1000000.0;
                        info->mdi_df_ms = snap.stats.mdi_df_ms;
                        
                        info->rst_net_s = snap.predictive.rst_network_s;
                        info->rst_enc_s = snap.predictive.rst_encoder_s;
                        info->drift_ppm = snap.predictive.stc_wall_drift_ppm;
                        info->drift_long_ppm = snap.predictive.long_term_drift_ppm;
                        
                        for (uint32_t j = 0; j < snap.active_pid_count; j++) {
                            if (snap.pids[j].width > 0 && info->width == 0) {
                                info->width = (double)snap.pids[j].width;
                                info->height = (double)snap.pids[j].height;
                                info->fps = (snap.pids[j].gop_ms > 0) ? ((double)snap.pids[j].gop_n * 1000.0 / snap.pids[j].gop_ms) : 0.0;
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
            
            atomic_store_explicit((_Atomic uint64_t*)&shm_block->seq_lock, seq + 2, memory_order_release); // Back to even
            last_shm_update = now;
        }
    }

    if (shm_block) munmap(shm_block, sizeof(tsa_top_shm_block_t));
    if (shm_fd >= 0) close(shm_fd);
    shm_unlink(TSA_TOP_SHM_NAME);

    mg_mgr_free(&mgr);

    pthread_join(t_io, NULL);
    for (int i = 0; i < WORKER_THREADS; i++) pthread_join(t_workers[i], NULL);
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
    return 0;
}
