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

#include "mongoose.h"
#include "spsc_queue.h"
#include "tsa.h"
#include "tsa_internal.h"


#include "tsa_source.h"

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

static void on_source_packets(void* user_data, const uint8_t* pkts, int count, uint64_t now_ns) {
    (void)user_data;
    if (now_ns == 0) now_ns = (uint64_t)ts_now_ns128();
    ts_packet_t pkt;
    for (int i = 0; i < count; i++) {
        memcpy(pkt.data, pkts + (i * 188), 188);
        pkt.timestamp_ns = now_ns;
        int backoff_cnt = 0;
        while (g_keep_running && !spsc_queue_push(q_cap_to_dec, &pkt)) backoff_sleep(backoff_cnt++);
    }
}

static void on_source_status(void* user_data, int status_code, const char* msg) {
    (void)user_data;
    fprintf(stderr, "CLI: Source Status [%d] %s\n", status_code, msg);
    if (status_code < 0) g_keep_running = 0;
}

/* 2. Decode Thread */
static void* decode_thread(void* arg) {
    tsa_handle_t* h = (tsa_handle_t*)arg;
    set_thread_affinity(1);
    ts_packet_t pkt;
    int backoff_cnt = 0;
    while (1) {
        if (spsc_queue_pop(q_cap_to_dec, &pkt)) {
            if (pkt.timestamp_ns == 0) break;
            tsa_feed_data(h, pkt.data, 188, pkt.timestamp_ns);
            while (g_keep_running && !spsc_queue_push(q_dec_to_met, &pkt)) backoff_sleep(backoff_cnt++);
            backoff_cnt = 0;
        } else {
            backoff_sleep(backoff_cnt++);
        }
    }
    pkt.timestamp_ns = 0;
    while (!spsc_queue_push(q_dec_to_met, &pkt)) backoff_sleep(100);
    printf("CLI: Decode/Sync finished.\n");
    return NULL;
}

/* 3. Metrology Thread */
static void* metrology_thread(void* arg) {
    tsa_handle_t* h = (tsa_handle_t*)arg;
    set_thread_affinity(2);
    ts_packet_t pkt;
    int backoff_cnt = 0;
    uint64_t last_snap_ts = 0;
    while (1) {
        if (spsc_queue_pop(q_dec_to_met, &pkt)) {
            if (pkt.timestamp_ns == 0) break;

            if (h->stc_ns - last_snap_ts > 100000000ULL) {
                tsa_commit_snapshot(h, h->stc_ns);
                last_snap_ts = h->stc_ns;
            }
            backoff_cnt = 0;
        } else {
            // Heartbeat: If no packets for 100ms real-time, commit a snapshot using real-time
            // to allow timeout/TTL logic to fire.
            struct timespec ts;
            clock_gettime(CLOCK_MONOTONIC, &ts);
            uint64_t now_ns = (uint64_t)ts.tv_sec * 1000000000ULL + ts.tv_nsec;
            
            // Only heartbeat if we are in LIVE mode
            if (h->stc_ns - last_snap_ts > 100000000ULL) {
                tsa_commit_snapshot(h, h->stc_ns);
                last_snap_ts = h->stc_ns;
            }
            
            backoff_sleep(backoff_cnt++);
            if (backoff_cnt > 100) {
                // If extremely idle, ensure we still commit occasionally
                tsa_commit_snapshot(h, h->stc_ns);
                usleep(50000); 
            }
        }
    }
    tsa_commit_snapshot(h, h->stc_ns);
    printf("CLI: Metrology finished.\n");
    g_keep_running = 0;
    return NULL;
}

/* 4. Output/HTTP Thread */
static void fn(struct mg_connection* c, int ev, void* ev_data) {
    tsa_handle_t* h = (tsa_handle_t*)c->mgr->userdata;
    if (ev == MG_EV_HTTP_MSG) {
        struct mg_http_message* hm = (struct mg_http_message*)ev_data;
        if (mg_match(hm->uri, mg_str("/metrics"), NULL) || mg_match(hm->uri, mg_str("/metrics/core"), NULL) ||
            mg_match(hm->uri, mg_str("/metrics/pids"), NULL)) {
            static char resp[128 * 1024];
            if (mg_match(hm->uri, mg_str("/metrics/core"), NULL)) {
                tsa_exporter_prom_core(&h, 1, resp, sizeof(resp));
            } else if (mg_match(hm->uri, mg_str("/metrics/pids"), NULL)) {
                tsa_exporter_prom_pids(&h, 1, resp, sizeof(resp));
            } else {
                tsa_exporter_prom_v2(&h, 1, resp, sizeof(resp));
            }
            mg_http_reply(c, 200, "Content-Type: text/plain\r\nAccess-Control-Allow-Origin: *\r\n", "%s", resp);
        } else if (mg_match(hm->uri, mg_str("/api/v1/snapshot"), NULL)) {
            static char resp[512 * 1024];
            tsa_snapshot_full_t snap;
            tsa_take_snapshot_full(h, &snap);
            tsa_snapshot_to_json(h, &snap, resp, sizeof(resp));
            mg_http_reply(c, 200, "Content-Type: application/json\r\nAccess-Control-Allow-Origin: *\r\n", "%s", resp);
        }
    }
}

static void* http_thread(void* arg) {
    struct mg_mgr* mgr = (struct mg_mgr*)arg;
    set_thread_affinity(3);
    if (mg_http_listen(mgr, "http://0.0.0.0:12345", fn, NULL) == NULL) return NULL;
    while (g_keep_running) mg_mgr_poll(mgr, 100);
    mg_mgr_free(mgr);
    return NULL;
}

static void print_usage(const char* prog) {
    printf("TSA - Transport Stream Analyzer (Professional Edition)\n");
    printf("Usage: %s [options] <input.ts>\n\n", prog);
    printf("Core Options:\n");
    printf("  -u, --udp <port>      Listen for incoming TS over UDP on specified port.\n");
    printf("  -s, --srt-url <url>   Listen for incoming SRT stream (e.g., srt://:9000).\n");
    printf("  -i, --interface <if>  PCAP/NIC Monitoring interface (e.g., eth0).\n");
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

    tsa_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.is_live = true;
    cfg.pcr_ema_alpha = 0.1;
    cfg.op_mode = TSA_MODE_LIVE;

    char filename[512] = "";
    int opt;
    char interface[64] = "";
    static struct option long_options[] = {{"udp", required_argument, 0, 'u'},
                                           {"srt-url", required_argument, 0, 's'},
                                           {"interface", required_argument, 0, 'i'},
                                           {"mode", required_argument, 0, 'm'},
                                           {"help", no_argument, 0, 'h'},
                                           {0, 0, 0, 0}};

    while ((opt = getopt_long(argc, argv, "u:s:i:m:h", long_options, NULL)) != -1) {
        switch (opt) {
            case 'h':
                print_usage(argv[0]);
                return 0;
            case 'u':
                cfg.udp_port = atoi(optarg);
                break;
            case 's':
                strncpy(cfg.url, optarg, sizeof(cfg.url) - 1);
                break;
            case 'i':
                strncpy(interface, optarg, sizeof(interface) - 1);
                break;
            case 'm':
                if (strcmp(optarg, "live") == 0)
                    cfg.op_mode = TSA_MODE_LIVE;
                else if (strcmp(optarg, "replay") == 0)
                    cfg.op_mode = TSA_MODE_REPLAY;
                else if (strcmp(optarg, "forensic") == 0)
                    cfg.op_mode = TSA_MODE_FORENSIC;
                else if (strcmp(optarg, "certification") == 0)
                    cfg.op_mode = TSA_MODE_CERTIFICATION;
                break;
        }
    }
    if (optind < argc) {
        strncpy(filename, argv[optind], sizeof(filename) - 1);
        strncpy(cfg.input_label, "CLI-FILE", sizeof(cfg.input_label) - 1);
    } else {
        strncpy(cfg.input_label, "CLI-NET", sizeof(cfg.input_label) - 1);
    }

    if (cfg.op_mode == TSA_MODE_CERTIFICATION) printf("CLI: Certification Mode Active.\n");

    tsa_handle_t* h = tsa_create(&cfg);
    q_cap_to_dec = spsc_queue_create(16384);
    q_dec_to_met = spsc_queue_create(16384);

    /* Initialize Source */
    tsa_source_callbacks_t source_cbs = { .on_packets = on_source_packets, .on_status = on_source_status };
    tsa_source_t* source = NULL;
    
    if (interface[0]) {
        source = tsa_source_create(TSA_SOURCE_PCAP, interface, &source_cbs, NULL);
    } else if (cfg.udp_port > 0) {
        char url[64]; snprintf(url, sizeof(url), "udp://0.0.0.0:%d", cfg.udp_port);
        source = tsa_source_create(TSA_SOURCE_UDP, url, &source_cbs, NULL);
    } else if (cfg.url[0]) {
        source = tsa_source_create(TSA_SOURCE_SRT, cfg.url, &source_cbs, NULL);
    } else if (filename[0]) {
        source = tsa_source_create(TSA_SOURCE_FILE, filename, &source_cbs, NULL);
    }

    if (!source) {
        fprintf(stderr, "Error: No valid input source specified.\n");
        return 1;
    }

    signal(SIGINT, sig_handler);
    pthread_t t_dec, t_met, t_http;
    tsa_source_start(source);
    pthread_create(&t_dec, NULL, decode_thread, h);
    pthread_create(&t_met, NULL, metrology_thread, h);

    struct mg_mgr mgr;
    mg_mgr_init(&mgr);
    mgr.userdata = h;
    pthread_create(&t_http, NULL, http_thread, &mgr);

    printf("CLI: 4-Thread Pipeline Started. Core Affinity [0,1,2,3]\n");
    while (g_keep_running) usleep(100000);

    tsa_source_stop(source);
    pthread_join(t_dec, NULL);
    pthread_join(t_met, NULL);
    pthread_join(t_http, NULL);
    tsa_source_destroy(source);

    // Dump final report
    tsa_snapshot_full_t snap;
    if (tsa_take_snapshot_full(h, &snap) == 0) {
        char* buf = malloc(256 * 1024);
        tsa_snapshot_to_json(h, &snap, buf, 256 * 1024);
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
    tsa_commit_snapshot(h, h->stc_ns);
    tsa_render_dashboard(h);
    tsa_destroy(h);
    printf("CLI: Shutdown Complete.\n");
    return 0;
}
