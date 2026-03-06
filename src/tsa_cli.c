#define _GNU_SOURCE
#include <arpa/inet.h>
#include <errno.h>
#include <getopt.h>
#include <pthread.h>
#include <sched.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>
#include <stdatomic.h>

#include "mongoose.h"
#include "spsc_queue.h"
#include "tsa.h"
#include "tsa_internal.h"
#include "tsa_source.h"

static _Atomic int g_keep_running = 1;
static spsc_queue_t* g_pkt_queue = NULL;
static uint64_t g_simulated_now_ns = NS_PER_SEC; /* Start at 1s for replay */

/* Prometheus exporter from src/tsa_exporter_prom.c */
extern void tsa_exporter_prom_v2(tsa_handle_t** handles, int count, char* buf, size_t sz);

static void sig_handler(int sig) { (void)sig; atomic_store(&g_keep_running, 0); }

/* --- Source Callbacks --- */

static void on_source_packets_async(void* user_data, const uint8_t* pkts, int count, uint64_t now_ns) {
    tsa_handle_t* h = (tsa_handle_t*)user_data;
    if (count <= 0 || !g_pkt_queue) return;

    uint64_t ts = (now_ns == 0) ? (uint64_t)ts_now_ns128() : now_ns;

    for (int i = 0; i < count; i++) {
        ts_packet_t pkt;
        memcpy(pkt.data, pkts + (i * 188), 188);
        pkt.timestamp_ns = ts;
        /* If queue is full, increment drop counter for visibility in metrics */
        if (!spsc_queue_push(g_pkt_queue, &pkt)) {
            if (h && h->live) h->live->internal_analyzer_drop++;
        }
    }
}
static void on_source_status(void* user_data, int status_code, const char* msg) {
    (void)user_data;
    if (status_code <= 0) {
        if (msg) fprintf(stderr, "Source status: %s\n", msg);
        g_keep_running = 0;
    }
}

/* --- HTTP Server --- */

static void http_handler(struct mg_connection* c, int ev, void* ev_data) {
    if (ev == MG_EV_HTTP_MSG) {
        struct mg_http_message* hm = (struct mg_http_message*)ev_data;
        tsa_handle_t* h = (tsa_handle_t*)c->mgr->userdata;

        if (mg_match(hm->uri, mg_str("/metrics"), NULL)) {
            /* Reuse a large thread-local buffer to avoid frequent heap churn for metrics */
            static __thread char* metrics_buf = NULL;
            const size_t buf_size = 1024 * 1024;
            if (!metrics_buf) metrics_buf = malloc(buf_size);

            if (metrics_buf) {
                tsa_exporter_prom_v2(&h, 1, metrics_buf, buf_size);
                mg_http_reply(c, 200, "Content-Type: text/plain\r\n", "%s", metrics_buf);
            } else {
                mg_http_reply(c, 500, NULL, "Out of memory");
            }
        } else if (mg_match(hm->uri, mg_str("/json"), NULL)) {
            tsa_snapshot_full_t snap;
            if (tsa_take_snapshot_full(h, &snap) == 0) {
                static __thread char* json_buf = NULL;
                const size_t json_buf_size = 1024 * 1024;
                if (!json_buf) json_buf = malloc(json_buf_size);

                if (json_buf) {
                    tsa_snapshot_to_json(h, &snap, json_buf, json_buf_size);
                    mg_http_reply(c, 200, "Content-Type: application/json\r\n", "%s", json_buf);
                } else {
                    mg_http_reply(c, 500, NULL, "Out of memory");
                }
            } else {
                mg_http_reply(c, 503, NULL, "Snapshot busy");
            }
        } else {
            mg_http_reply(c, 404, NULL, "Not Found");
        }
    }
}

static void* http_thread_func(void* arg) {
    struct mg_mgr* mgr = (struct mg_mgr*)arg;
    while (g_keep_running) {
        mg_mgr_poll(mgr, 100);
    }
    return NULL;
}

/* --- Analysis Logic --- */

static void* analysis_thread_func(void* arg) {
    tsa_handle_t* h = (tsa_handle_t*)arg;
    ts_packet_t pkt;
    uint64_t last_snap_vstc = 0;

    while (g_keep_running) {
        if (spsc_queue_pop(g_pkt_queue, &pkt)) {
            tsa_feed_data(h, pkt.data, 188, pkt.timestamp_ns);

            /* Snapshot trigger: every 100ms of virtual time or 5000 packets */
            if (h->stc_locked && (h->stc_ns - last_snap_vstc >= 100000000ULL)) {
                tsa_commit_snapshot(h, pkt.timestamp_ns);
                last_snap_vstc = h->stc_ns;
            }
        } else {
            struct timespec ts = {0, 1000000}; /* 1ms sleep */
            nanosleep(&ts, NULL);
        }
    }
    return NULL;
}

/* --- CLI Utils --- */

static void print_usage(const char* prog) {
    printf("TsAnalyzer CLI - Professional Metrology Tool\n");
    printf("Usage: %s [options] <file.ts>\n", prog);
    printf("Options:\n");
    printf("  -m, --mode <live|replay>  Set operation mode (default: replay for file, live for others)\n");
    printf("  -u, --udp <port>          Listen for UDP input on port\n");
    printf("  -s, --srt <url>           Connect to/Listen for SRT input\n");
    printf("  -i, --interface <iface>   Capture from network interface (PCAP)\n");
    printf("  -p, --pacing              Enable source-level pacing (for replay/file)\n");
    printf("  -l, --label <id>          Set stream label (default: unknown)\n");
    printf("  -H, --http <port>         Set HTTP server port (default: 12345)\n");
    printf("  -a, --alpha <val>         Set PCR EMA alpha (default: 0.1)\n");
}

int main(int argc, char** argv) {
    tsa_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.pcr_ema_alpha = 0.1;
    cfg.op_mode = TSA_MODE_REPLAY; /* Default */

    int http_port = 12345;
    int udp_port = 0;
    char srt_url[256] = "";
    char interface[64] = "";
    char filename[512] = "";
    char stream_label[32] = "";
    bool pacing = false;

    static struct option long_options[] = {
        {"mode",      required_argument, 0, 'm'},
        {"udp",       required_argument, 0, 'u'},
        {"srt",       required_argument, 0, 's'},
        {"interface", required_argument, 0, 'i'},
        {"pacing",    no_argument,       0, 'p'},
        {"label",     required_argument, 0, 'l'},
        {"http",      required_argument, 0, 'H'},
        {"alpha",     required_argument, 0, 'a'},
        {"help",      no_argument,       0, 'h'},
        {0, 0, 0, 0}
    };

    int opt;
    while ((opt = getopt_long(argc, argv, "m:u:s:i:pl:H:a:h", long_options, NULL)) != -1) {
        switch (opt) {
            case 'm':
                if (strcasecmp(optarg, "live") == 0) cfg.op_mode = TSA_MODE_LIVE;
                else if (strcasecmp(optarg, "replay") == 0) cfg.op_mode = TSA_MODE_REPLAY;
                break;
            case 'u': udp_port = atoi(optarg); cfg.op_mode = TSA_MODE_LIVE; break;
            case 's': strncpy(srt_url, optarg, sizeof(srt_url)-1); cfg.op_mode = TSA_MODE_LIVE; break;
            case 'i': strncpy(interface, optarg, sizeof(interface)-1); cfg.op_mode = TSA_MODE_LIVE; break;
            case 'p': pacing = true; break;
            case 'l': strncpy(stream_label, optarg, sizeof(stream_label)-1); break;
            case 'H': http_port = atoi(optarg); break;
            case 'a': cfg.pcr_ema_alpha = atof(optarg); break;
            case 'h': print_usage(argv[0]); return 0;
            default: return 1;
        }
    }

    if (optind < argc) {
        strncpy(filename, argv[optind], sizeof(filename)-1);
    }

    if (!filename[0] && udp_port == 0 && !srt_url[0] && !interface[0]) {
        print_usage(argv[0]);
        return 1;
    }

    if (stream_label[0]) {
        strncpy(cfg.input_label, stream_label, sizeof(cfg.input_label)-1);
    }

    signal(SIGINT, sig_handler);
    signal(SIGTERM, sig_handler);

    tsa_handle_t* h = tsa_create(&cfg);
    if (!h) {
        fprintf(stderr, "Failed to create TSA handle\n");
        return 1;
    }

    /* Setup HTTP server if needed */
    struct mg_mgr mgr;
    pthread_t http_tid = 0;
    if (http_port > 0) {
        mg_mgr_init(&mgr);
        mgr.userdata = h;
        char bind_addr[64];
        snprintf(bind_addr, sizeof(bind_addr), "http://0.0.0.0:%d", http_port);
        if (mg_http_listen(&mgr, bind_addr, http_handler, NULL) == NULL) {
            fprintf(stderr, "FATAL: Failed to listen on %s\n", bind_addr);
            goto cleanup;
        } else {
            pthread_create(&http_tid, NULL, http_thread_func, &mgr);
        }
    }

    if (cfg.op_mode == TSA_MODE_LIVE) {
        /* Live mode uses threads and queue */
        g_pkt_queue = spsc_queue_create(131072); /* 128k packets ~ 24MB */

        pthread_t analysis_tid;
        pthread_create(&analysis_tid, NULL, analysis_thread_func, h);

        tsa_source_callbacks_t cbs = { on_source_packets_async, on_source_status };
        tsa_source_t* src = NULL;
        if (interface[0]) src = tsa_source_create(TSA_SOURCE_PCAP, interface, NULL, 0, &cbs, h);
        else if (udp_port > 0) src = tsa_source_create(TSA_SOURCE_UDP, NULL, NULL, udp_port, &cbs, h);
        else if (srt_url[0]) src = tsa_source_create(TSA_SOURCE_SRT, srt_url, NULL, 0, &cbs, h);
        else if (filename[0]) src = tsa_source_create(TSA_SOURCE_FILE, filename, NULL, 0, &cbs, h);

        if (src) {
            tsa_source_set_pacing(src, pacing);
            tsa_source_start(src);
            while (g_keep_running) {
                usleep(100000);
            }
            tsa_source_stop(src);
            tsa_source_destroy(src);
        }

        pthread_join(analysis_tid, NULL);
        spsc_queue_destroy(g_pkt_queue);
    } else {
        /* Replay mode: simple synchronous loop for maximum determinism */
        FILE* fp = fopen(filename, "rb");
        if (!fp) {
            perror("fopen");
            goto cleanup;
        }

        uint8_t buf[188 * 100];
        size_t n;
        uint64_t last_snap_vstc = 0;

        while (g_keep_running && (n = fread(buf, 1, sizeof(buf), fp)) > 0) {
            int pkts = n / 188;
            for (int i = 0; i < pkts; i++) {
                /* Virtual clock: nominal 10Mbps = 150.4us per packet */
                g_simulated_now_ns += NOMINAL_PACKET_INTERVAL_NS;
                tsa_feed_data(h, buf + (i * 188), 188, g_simulated_now_ns);

                if (h->stc_locked && (h->stc_ns - last_snap_vstc >= 100000000ULL)) {
                    tsa_commit_snapshot(h, g_simulated_now_ns);
                    last_snap_vstc = h->stc_ns;
                }
            }
            if (pacing) usleep(1000);
        }
        tsa_commit_snapshot(h, g_simulated_now_ns);
        fclose(fp);
    }

cleanup:
    g_keep_running = 0;
    if (http_tid) {
        pthread_join(http_tid, NULL);
        mg_mgr_free(&mgr);
    }

    /* Final JSON output */
    tsa_snapshot_full_t final_snap;
    if (tsa_take_snapshot_full(h, &final_snap) == 0) {
        char* jbuf = malloc(1024 * 1024);
        if (jbuf) {
            tsa_snapshot_to_json(h, &final_snap, jbuf, 1024 * 1024);
            FILE* f_out = fopen("final_metrology.json", "w");
            if (f_out) {
                fprintf(f_out, "%s\n", jbuf);
                fclose(f_out);
            }
            free(jbuf);
        }
    }

    tsa_destroy(h);
    return 0;
}
