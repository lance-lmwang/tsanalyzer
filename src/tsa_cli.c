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
    ts_packet_t pkt;
    if (count == 0) {
        /* Poison pill: signal end of stream */
        pkt.timestamp_ns = 0;
        int backoff_cnt = 0;
        while (!spsc_queue_push(q_cap_to_dec, &pkt)) backoff_sleep(backoff_cnt++);
        return;
    }
    if (now_ns == 0) now_ns = (uint64_t)ts_now_ns128();
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
    if (status_code <= 0) g_keep_running = 0;
}

static void* decode_thread(void* arg) {
    tsa_handle_t* h = (tsa_handle_t*)arg;
    set_thread_affinity(1);
    ts_packet_t pkt;
    int backoff_cnt = 0;
    while (1) {
        if (spsc_queue_pop(q_cap_to_dec, &pkt)) {
            if (pkt.timestamp_ns == 0) break;
            tsa_feed_data(h, pkt.data, 188, pkt.timestamp_ns);
            pkt.stc_ns = h->stc_ns; 
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

static void* metrology_thread(void* arg) {
    tsa_handle_t* h = (tsa_handle_t*)arg;
    set_thread_affinity(2);
    ts_packet_t pkt;
    int backoff_cnt = 0;
    uint64_t last_snap_ts = 0;
    while (1) {
        if (spsc_queue_pop(q_dec_to_met, &pkt)) {
            if (pkt.timestamp_ns == 0) break;
            if (!h->pending_snapshot && pkt.stc_ns - last_snap_ts > 100000000ULL) {
                h->snapshot_stc = pkt.stc_ns;
                h->pending_snapshot = true;
                last_snap_ts = pkt.stc_ns;
            }
            backoff_cnt = 0;
        } else {
            if (h->op_mode == TSA_MODE_REPLAY) {
                backoff_sleep(backoff_cnt++);
                if (backoff_cnt > 1000) usleep(1000);
                continue;
            }
            usleep(50000);
        }
    }
    printf("CLI: Metrology finished.\n");
    return NULL;
}

static void* http_thread(void* arg) {
    struct mg_mgr* mgr = (struct mg_mgr*)arg;
    set_thread_affinity(3);
    while (g_keep_running) {
        mg_mgr_poll(mgr, 100);
    }
    return NULL;
}

static void http_fn(struct mg_connection* c, int ev, void* ev_data) {
    tsa_handle_t* h = (tsa_handle_t*)c->mgr->userdata;

    if (ev == MG_EV_HTTP_MSG) {
        struct mg_http_message* hm = (struct mg_http_message*)ev_data;
        if (mg_match(hm->uri, mg_str("/metrics"), NULL)) {
            char* buf = malloc(256 * 1024);
            tsa_exporter_prom_v2(&h, 1, buf, 256 * 1024);
            mg_http_reply(c, 200, "Content-Type: text/plain\r\n", "%s", buf);
            free(buf);
        } else {
            tsa_snapshot_full_t snap;
            if (tsa_take_snapshot_full(h, &snap) == 0) {
                char* buf = malloc(256 * 1024);
                tsa_snapshot_to_json(h, &snap, buf, 256 * 1024);
                mg_http_reply(c, 200, "Content-Type: application/json\r\n", "%s", buf);
                free(buf);
            } else {
                mg_http_reply(c, 503, "", "Busy");
            }
        }
    }
}

static void print_usage(const char* prog) {
    printf("Usage: %s [options] <input_file.ts | input_file.pcap>\n", prog);
    printf("Options:\n");
    printf("  -u, --udp <port>       Listen on UDP port\n");
    printf("  -s, --srt-url <url>    SRT URL (e.g. srt://:9000)\n");
    printf("  -i, --interface <dev>  Live PCAP capture on interface\n");
    printf("  --dst-ip <ip>          Filter by destination IP (PCAP only)\n");
    printf("  --dst-port <port>      Filter by destination UDP port (PCAP only)\n");
    printf("  -p, --pacing           Simulate original capture speed during replay\n");
    printf("  -m, --mode <mode>      Operation mode: live, replay, forensic, certification\n");
    printf("  -h, --help             Show this help\n");
}

static void parse_args(int argc, char** argv, tsa_config_t* cfg, char* filename, size_t filename_sz, char* interface, size_t interface_sz, char* filter_ip, size_t filter_ip_sz, int* filter_port, bool* pacing) {
    int opt;
    static struct option long_options[] = {
        {"udp", required_argument, 0, 'u'},
        {"srt-url", required_argument, 0, 's'},
        {"interface", required_argument, 0, 'i'},
        {"mode", required_argument, 0, 'm'},
        {"pacing", no_argument, 0, 'p'},
        {"dst-ip", required_argument, 0, 1001},
        {"dst-port", required_argument, 0, 1002},
        {"help", no_argument, 0, 'h'},
        {0, 0, 0, 0}};

    while ((opt = getopt_long(argc, argv, "u:s:i:m:ph", long_options, NULL)) != -1) {
        switch (opt) {
            case 'h': print_usage(argv[0]); exit(0);
            case 'u': cfg->udp_port = atoi(optarg); break;
            case 's': strncpy(cfg->url, optarg, sizeof(cfg->url) - 1); break;
            case 'i': strncpy(interface, optarg, interface_sz - 1); break;
            case 'p': *pacing = true; break;
            case 1001: strncpy(filter_ip, optarg, filter_ip_sz - 1); break;
            case 1002: *filter_port = atoi(optarg); break;
            case 'm':
                if (strcmp(optarg, "live") == 0) cfg->op_mode = TSA_MODE_LIVE;
                else if (strcmp(optarg, "replay") == 0) cfg->op_mode = TSA_MODE_REPLAY;
                else if (strcmp(optarg, "forensic") == 0) cfg->op_mode = TSA_MODE_FORENSIC;
                else if (strcmp(optarg, "certification") == 0) cfg->op_mode = TSA_MODE_CERTIFICATION;
                break;
        }
    }
    if (optind < argc) {
        strncpy(filename, argv[optind], filename_sz - 1);
        strncpy(cfg->input_label, "CLI-FILE", sizeof(cfg->input_label) - 1);
        if (cfg->op_mode == TSA_MODE_LIVE) cfg->op_mode = TSA_MODE_REPLAY;
    } else {
        strncpy(cfg->input_label, "CLI-NET", sizeof(cfg->input_label) - 1);
    }
}

static tsa_source_t* init_source(tsa_config_t* cfg, const char* interface, const char* filename, const char* filter_ip, int filter_port, tsa_source_callbacks_t* cbs) {
    if (interface[0]) {
        return tsa_source_create(TSA_SOURCE_PCAP, interface, filter_ip, filter_port, cbs, NULL);
    } else if (cfg->udp_port > 0) {
        char url[64]; snprintf(url, sizeof(url), "udp://0.0.0.0:%d", cfg->udp_port);
        return tsa_source_create(TSA_SOURCE_UDP, url, NULL, 0, cbs, NULL);
    } else if (cfg->url[0]) {
        return tsa_source_create(TSA_SOURCE_SRT, cfg->url, NULL, 0, cbs, NULL);
    } else if (filename[0]) {
        if (strstr(filename, ".pcap")) {
            return tsa_source_create(TSA_SOURCE_PCAP, filename, filter_ip, filter_port, cbs, NULL);
        }
        return tsa_source_create(TSA_SOURCE_FILE, filename, NULL, 0, cbs, NULL);
    }
    return NULL;
}

static void dump_final_report(tsa_handle_t* h) {
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
}

int main(int argc, char** argv) {
    if (argc < 2) { print_usage(argv[0]); return 1; }
    tsa_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.is_live = true;
    cfg.pcr_ema_alpha = 0.1;
    cfg.op_mode = TSA_MODE_LIVE;

    char filename[512] = "";
    char interface[64] = "";
    char filter_ip[64] = "";
    int filter_port = 0;
    bool pacing = false;
    parse_args(argc, argv, &cfg, filename, sizeof(filename), interface, sizeof(interface), filter_ip, sizeof(filter_ip), &filter_port, &pacing);

    tsa_handle_t* h = tsa_create(&cfg);
    q_cap_to_dec = spsc_queue_create(16384);
    q_dec_to_met = spsc_queue_create(16384);

    tsa_source_callbacks_t source_cbs = { .on_packets = on_source_packets, .on_status = on_source_status };
    tsa_source_t* source = init_source(&cfg, interface, filename, filter_ip, filter_port, &source_cbs);

    if (!source) { fprintf(stderr, "Error: No valid input source.\n"); return 1; }
    tsa_source_set_pacing(source, pacing);

    signal(SIGINT, sig_handler);
    pthread_t t_dec, t_met, t_http;
    tsa_source_start(source);
    pthread_create(&t_dec, NULL, decode_thread, h);
    pthread_create(&t_met, NULL, metrology_thread, h);

    struct mg_mgr mgr;
    mg_mgr_init(&mgr);
    mgr.userdata = h;
    mg_http_listen(&mgr, "http://0.0.0.0:12345", http_fn, NULL);
    pthread_create(&t_http, NULL, http_thread, &mgr);

    printf("CLI: Pipeline Started. Pacing: [%s] Filter: [%s:%d]\n", pacing?"ON":"OFF", filter_ip[0]?filter_ip:"ANY", filter_port);
    while (g_keep_running) usleep(100000);

    tsa_source_stop(source);
    pthread_join(t_dec, NULL);
    pthread_join(t_met, NULL);
    pthread_join(t_http, NULL);
    tsa_source_destroy(source);
    dump_final_report(h);
    mg_mgr_free(&mgr);
    spsc_queue_destroy(q_cap_to_dec);
    spsc_queue_destroy(q_dec_to_met);
    tsa_destroy(h);
    return 0;
}
