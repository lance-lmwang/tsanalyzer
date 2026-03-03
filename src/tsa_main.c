#define _GNU_SOURCE
#include <arpa/inet.h>
#include <errno.h>
#include <getopt.h>
#include <pthread.h>
#include <sched.h>
#include <signal.h>
#include <srt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include "../deps/mongoose/mongoose.h"
#include "spsc_queue.h"
#include "tsa.h"
#include "tsa_internal.h"

static tsa_handle_t* g_h = NULL;
static volatile int g_keep_running = 1;

/* Queues for the pipeline */
static spsc_queue_t* q_cap_to_dec;
static spsc_queue_t* q_dec_to_met;

/* Industrial Prometheus Exporter */
extern void tsa_exporter_prom_v2(tsa_handle_t** handles, int count, char* buf, size_t sz);

static void sig_handler(int sig) {
    (void)sig;
    g_keep_running = 0;
}

static void set_thread_affinity(int core_id) {
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(core_id, &cpuset);
    pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
}

static inline void cpu_relax(void) {
#if defined(__x86_64__) || defined(__i386__)
    __asm__ volatile("rep; nop" ::: "memory");
#else
    __asm__ volatile("" ::: "memory");
#endif
}

static void backoff_sleep(int count) {
    if (count < 10) {
        cpu_relax();
    } else if (count < 50) {
        for (int i = 0; i < 10; i++) cpu_relax();
    } else {
        struct timespec ts = {0, 1000};  // 1us
        nanosleep(&ts, NULL);
    }
}

typedef struct {
    tsa_config_t cfg;
    char filename[512];
} capture_args_t;

/* 1. Capture Thread - Supports File, UDP, and SRT */
static void* capture_thread(void* arg) {
    capture_args_t* args = (capture_args_t*)arg;
    tsa_config_t* cfg = &args->cfg;
    set_thread_affinity(0);
    ts_packet_t pkt;
    uint8_t raw_buf[1500];
    int backoff_cnt = 0;

    if (cfg->op_mode == TSA_MODE_REPLAY && strlen(args->filename) > 0) {
        FILE* f = fopen(args->filename, "rb");
        if (!f) {
            fprintf(stderr, "[FATAL ERROR] Cannot open input TS file: '%s'\n", args->filename);
            g_keep_running = 0;
            return NULL;
        }
        printf("CLI: Replay Mode Active (File: %s)\n", args->filename);
        uint64_t simulated_now_ns = 1704067200000000000ULL;
        while (g_keep_running && fread(pkt.data, 1, 188, f) == 188) {
            pkt.timestamp_ns = simulated_now_ns;
            while (g_keep_running && !spsc_queue_push(q_cap_to_dec, &pkt)) backoff_sleep(backoff_cnt++);
            backoff_cnt = 0;
            simulated_now_ns += 188ULL * 8 * 1000000000 / 10000000;
        }
        fclose(f);
    } else if (cfg->udp_port > 0) {
        int fd = socket(AF_INET, SOCK_DGRAM, 0);
        struct sockaddr_in sa = {0};
        sa.sin_family = AF_INET;
        sa.sin_port = htons(cfg->udp_port);
        sa.sin_addr.s_addr = INADDR_ANY;
        if (bind(fd, (struct sockaddr*)&sa, sizeof(sa)) < 0) {
            fprintf(stderr, "[FATAL] UDP Bind failed on %d\n", cfg->udp_port);
            g_keep_running = 0;
            return NULL;
        }
        printf("CLI: UDP Live Capture Active (Port: %d)\n", cfg->udp_port);
        while (g_keep_running) {
            ssize_t len = recv(fd, raw_buf, sizeof(raw_buf), MSG_DONTWAIT);
            if (len > 0) {
                uint64_t now = (uint64_t)ts_now_ns128();
                for (int i = 0; i < len / 188; i++) {
                    memcpy(pkt.data, raw_buf + (i * 188), 188);
                    pkt.timestamp_ns = now;
                    while (g_keep_running && !spsc_queue_push(q_cap_to_dec, &pkt)) backoff_sleep(backoff_cnt++);
                    backoff_cnt = 0;
                }
            } else
                usleep(1000);
        }
        close(fd);
    } else if (strlen(cfg->srt_url) > 0) {
        srt_startup();
        SRTSOCKET sock = srt_create_socket();
        struct sockaddr_in sa = {0};
        char addr_str[256];
        int port = 9000;
        sscanf(cfg->srt_url, "srt://%[^:]:%d", addr_str, &port);
        sa.sin_family = AF_INET;
        sa.sin_port = htons(port);
        sa.sin_addr.s_addr = (strlen(addr_str) == 0 || addr_str[0] == ':') ? INADDR_ANY : inet_addr(addr_str);
        int yes = 1;
        srt_setsockopt(sock, 0, SRTO_RCVSYN, &yes, sizeof(yes));
        if (srt_bind(sock, (struct sockaddr*)&sa, sizeof(sa)) != SRT_ERROR) {
            srt_listen(sock, 1);
            printf("CLI: SRT Live Capture Active (%s)\n", cfg->srt_url);
            while (g_keep_running) {
                struct sockaddr_in cli;
                int clen = sizeof(cli);
                SRTSOCKET conn = srt_accept(sock, (struct sockaddr*)&cli, &clen);
                if (conn == SRT_INVALID_SOCK) {
                    usleep(100000);
                    continue;
                }
                while (g_keep_running) {
                    int len = srt_recvmsg(conn, (char*)raw_buf, sizeof(raw_buf));
                    if (len > 0) {
                        uint64_t now = (uint64_t)ts_now_ns128();
                        for (int i = 0; i < len / 188; i++) {
                            memcpy(pkt.data, raw_buf + (i * 188), 188);
                            pkt.timestamp_ns = now;
                            while (g_keep_running && !spsc_queue_push(q_cap_to_dec, &pkt)) backoff_sleep(backoff_cnt++);
                            backoff_cnt = 0;
                        }
                    } else if (len == SRT_ERROR)
                        break;
                }
                srt_close(conn);
            }
        }
        srt_close(sock);
        srt_cleanup();
    }

    pkt.timestamp_ns = 0;  // Poison pill
    while (!spsc_queue_push(q_cap_to_dec, &pkt)) backoff_sleep(100);
    printf("CLI: Capture finished.\n");
    return NULL;
}

/* 2. Decode Thread */
static void* decode_thread(void* arg) {
    (void)arg;
    set_thread_affinity(1);
    ts_packet_t pkt;
    int backoff_cnt = 0;
    while (1) {
        if (spsc_queue_pop(q_cap_to_dec, &pkt)) {
            if (pkt.timestamp_ns == 0) break;
            while (g_keep_running && !spsc_queue_push(q_dec_to_met, &pkt)) backoff_sleep(backoff_cnt++);
            backoff_cnt = 0;
        } else {
            backoff_sleep(backoff_cnt++);
        }
    }
    pkt.timestamp_ns = 0;
    while (!spsc_queue_push(q_dec_to_met, &pkt)) backoff_sleep(100);
    printf("CLI: Decode finished.\n");
    return NULL;
}

/* 3. Metrology Thread */
static void* metrology_thread(void* arg) {
    (void)arg;
    set_thread_affinity(2);
    ts_packet_t pkt;
    int backoff_cnt = 0;
    uint64_t last_ts = 0;
    uint64_t last_snap_ts = 0;
    while (1) {
        if (spsc_queue_pop(q_dec_to_met, &pkt)) {
            if (pkt.timestamp_ns == 0) break;
            tsa_process_packet(g_h, pkt.data, pkt.timestamp_ns);
            last_ts = pkt.timestamp_ns;
            
            // Periodically commit snapshot (every 100ms)
            if (last_ts - last_snap_ts > 100000000ULL) {
                tsa_commit_snapshot(g_h, last_ts);
                last_snap_ts = last_ts;
            }
            backoff_cnt = 0;
        } else {
            backoff_sleep(backoff_cnt++);
        }
    }
    if (last_ts > 0) tsa_commit_snapshot(g_h, last_ts);
    printf("CLI: Metrology finished.\n");
    g_keep_running = 0;
    return NULL;
}

/* 4. Output/HTTP Thread */
static void fn(struct mg_connection* c, int ev, void* ev_data) {
    if (ev == MG_EV_HTTP_MSG) {
        struct mg_http_message* hm = (struct mg_http_message*)ev_data;
        if (mg_match(hm->uri, mg_str("/metrics"), NULL) ||
            mg_match(hm->uri, mg_str("/metrics/core"), NULL) ||
            mg_match(hm->uri, mg_str("/metrics/pids"), NULL)) {
            static char resp[128 * 1024];
            if (mg_match(hm->uri, mg_str("/metrics/core"), NULL)) {
                tsa_exporter_prom_core(&g_h, 1, resp, sizeof(resp));
            } else if (mg_match(hm->uri, mg_str("/metrics/pids"), NULL)) {
                tsa_exporter_prom_pids(&g_h, 1, resp, sizeof(resp));
            } else {
                tsa_exporter_prom_v2(&g_h, 1, resp, sizeof(resp));
            }
            mg_http_reply(c, 200, "Content-Type: text/plain\r\nAccess-Control-Allow-Origin: *\r\n", "%s", resp);
        } else if (mg_match(hm->uri, mg_str("/api/v1/snapshot"), NULL)) {
            static char resp[512 * 1024];
            tsa_snapshot_full_t snap;
            tsa_take_snapshot_full(g_h, &snap);
            tsa_snapshot_to_json(&snap, resp, sizeof(resp));
            mg_http_reply(c, 200, "Content-Type: application/json\r\nAccess-Control-Allow-Origin: *\r\n", "%s", resp);
        }
    }
}

static void* http_thread(void* arg) {
    (void)arg;
    set_thread_affinity(3);
    struct mg_mgr mgr;
    mg_mgr_init(&mgr);
    if (mg_http_listen(&mgr, "http://0.0.0.0:12345", fn, NULL) == NULL) return NULL;
    while (g_keep_running) mg_mgr_poll(&mgr, 100);
    mg_mgr_free(&mgr);
    return NULL;
}

static void print_usage(const char* prog) {
    printf("TSA - Transport Stream Analyzer (Professional Edition)\n");
    printf("Usage: %s [options] <input.ts>\n\n", prog);
    printf("Core Options:\n");
    printf("  -u, --udp <port>      Listen for incoming TS over UDP on specified port.\n");
    printf("  -s, --srt-url <url>   Listen for incoming SRT stream (e.g., srt://:9000).\n");
    printf("  -m, --mode <mode>     Operation mode: live, replay, forensic, certification.\n");
    printf("                        - live: Real-time analysis with system clock mapping.\n");
    printf("                        - replay: Fastest possible analysis from file (simulated time).\n");
    printf("                        - forensic: Deep analysis with persistent state recovery.\n\n");
    printf("HTTP Metrics:\n");
    printf("  Metrics available at http://localhost:12345/metrics (Prometheus format)\n");
    printf("  Full snapshot API: http://localhost:12345/api/v1/snapshot\n\n");
    printf("Examples:\n");
    printf("  Analysis of a local file:   %s --mode=replay sample.ts\n", prog);
    printf("  Real-time UDP monitoring:  %s --mode=live --udp 1234\n", prog);
    printf("  Real-time SRT monitoring:  %s --mode=live --srt-url srt://:9000\n", prog);
}

int main(int argc, char** argv) {
    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }

    capture_args_t cap_args;
    memset(&cap_args, 0, sizeof(cap_args));
    cap_args.cfg.is_live = true;
    cap_args.cfg.pcr_ema_alpha = 0.1;
    cap_args.cfg.op_mode = TSA_MODE_LIVE;

    int opt;
    static struct option long_options[] = {{"udp", required_argument, 0, 'u'},
                                           {"srt-url", required_argument, 0, 's'},
                                           {"mode", required_argument, 0, 'm'},
                                           {"help", no_argument, 0, 'h'},
                                           {0, 0, 0, 0}};

    while ((opt = getopt_long(argc, argv, "u:s:m:h", long_options, NULL)) != -1) {
        switch (opt) {
            case 'h':
                print_usage(argv[0]);
                return 0;
            case 'u':
                cap_args.cfg.udp_port = atoi(optarg);
                break;
            case 's':
                strncpy(cap_args.cfg.srt_url, optarg, sizeof(cap_args.cfg.srt_url) - 1);
                break;
            case 'm':
                if (strcmp(optarg, "live") == 0)
                    cap_args.cfg.op_mode = TSA_MODE_LIVE;
                else if (strcmp(optarg, "replay") == 0)
                    cap_args.cfg.op_mode = TSA_MODE_REPLAY;
                else if (strcmp(optarg, "forensic") == 0)
                    cap_args.cfg.op_mode = TSA_MODE_FORENSIC;
                else if (strcmp(optarg, "certification") == 0)
                    cap_args.cfg.op_mode = TSA_MODE_CERTIFICATION;
                break;
        }
    }
    if (optind < argc) {
        strncpy(cap_args.filename, argv[optind], sizeof(cap_args.filename) - 1);
        strncpy(cap_args.cfg.input_label, "CLI-FILE", sizeof(cap_args.cfg.input_label) - 1);
    } else {
        strncpy(cap_args.cfg.input_label, "CLI-NET", sizeof(cap_args.cfg.input_label) - 1);
    }

    if (cap_args.cfg.op_mode == TSA_MODE_CERTIFICATION) printf("CLI: Certification Mode Active.\n");

    g_h = tsa_create(&cap_args.cfg);
    q_cap_to_dec = spsc_queue_create(16384);
    q_dec_to_met = spsc_queue_create(16384);

    signal(SIGINT, sig_handler);
    pthread_t t_cap, t_dec, t_met, t_http;
    pthread_create(&t_cap, NULL, capture_thread, &cap_args);
    pthread_create(&t_dec, NULL, decode_thread, NULL);
    pthread_create(&t_met, NULL, metrology_thread, NULL);
    pthread_create(&t_http, NULL, http_thread, NULL);

    printf("CLI: 4-Thread Pipeline Started. Core Affinity [0,1,2,3]\n");
    while (g_keep_running) usleep(100000);

    pthread_join(t_cap, NULL);
    pthread_join(t_dec, NULL);
    pthread_join(t_met, NULL);
    pthread_join(t_http, NULL);

    // Dump final report
    tsa_snapshot_full_t snap;
    if (tsa_take_snapshot_full(g_h, &snap) == 0) {
        char* buf = malloc(256 * 1024);
        tsa_snapshot_to_json(&snap, buf, 256 * 1024);
        FILE* f_out = fopen("final_metrology.json", "w");
        if (f_out) {
            fprintf(f_out, "%s\n", buf);
            fclose(f_out);
            printf("CLI: Final metrology saved.\n");
        }
        free(buf);
    }

    spsc_queue_destroy(q_cap_to_dec);
    spsc_queue_destroy(q_dec_to_met);
    tsa_destroy(g_h);
    printf("CLI: Shutdown Complete.\n");
    return 0;
}
