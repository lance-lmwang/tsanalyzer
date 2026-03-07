#include <getopt.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "mongoose.h"
#include "tsa.h"
#include "tsa_log.h"
#include "tsp.h"

#define TAG "TSG"

static volatile int g_keep_running = 1;
static tsa_gateway_t* g_gw = NULL;
static char g_http_url[64] = "http://0.0.0.0:8001";

static void sig_handler(int sig) {
    (void)sig;
    g_keep_running = 0;
}

static void fn(struct mg_connection* c, int ev, void* ev_data) {
    if (ev == MG_EV_HTTP_MSG) {
        struct mg_http_message* hm = (struct mg_http_message*)ev_data;
        if (mg_match(hm->uri, mg_str("/metrics"), NULL)) {
            char* buf = malloc(128 * 1024);
            if (buf) {
                tsa_export_prometheus(tsa_gateway_get_tsa_handle(g_gw), buf, 128 * 1024);
                mg_http_reply(c, 200, "Content-Type: text/plain\r\n", "%s", buf);
                free(buf);
            } else {
                mg_http_reply(c, 500, NULL, "Out of memory");
            }
        } else {
            mg_http_reply(c, 404, NULL, "Not Found");
        }
    }
}

static void* http_server_thread(void* arg) {
    const char* listen_url = (const char*)arg;
    struct mg_mgr mgr;
    mg_mgr_init(&mgr);
    if (mg_http_listen(&mgr, listen_url, fn, NULL) == NULL) {
        tsa_error(TAG, "Failed to start HTTP server on %s", listen_url);
        return NULL;
    }
    tsa_info(TAG, "Gateway Metrics server started at %s/metrics", listen_url);
    while (g_keep_running) {
        mg_mgr_poll(&mgr, 1000);
    }
    mg_mgr_free(&mgr);
    return NULL;
}

uint64_t get_now_ns() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + ts.tv_nsec;
}

void print_usage(const char* prog) {
    printf("Usage: %s [options]\n", prog);
    printf("  --srt-in <url>       SRT Ingest URL (e.g. srt://:9000)\n");
    printf("  --udp-in <port>      UDP Ingest Port\n");
    printf("  --dest-ip <ip>       Egress Destination IP\n");
    printf("  --dest-port <port>   Egress Destination Port\n");
    printf("  --bitrate <bps>      Forced egress bitrate (CBR)\n");
    printf("  --pacing-pcr         Use PCR-locked pacing mode\n");
    printf("  --repair-cc          Enable CC-Loss Null Substitution\n");
    printf("  --repair-pcr         Enable PCR Re-stamping (De-jitter)\n");
    printf("  --forensics          Enable Forensic Capture\n");
    printf("  --http <url>         Metrics server URL (default: http://0.0.0.0:8001)\n");
    printf("  -h, --help           Show this help\n");
}

int main(int argc, char** argv) {
    tsa_gateway_config_t cfg = {0};
    cfg.analysis_primary.analysis.pcr_ema_alpha = 0.01;
    cfg.pacing.ts_per_udp = 7;
    cfg.pacing.mode = TSPACER_MODE_BASIC;

    const char* srt_in = NULL;
    int udp_in_port = 0;
    bool pacing_pcr = false;

    static struct option long_options[] = {{"srt-in", required_argument, 0, 1},  {"udp-in", required_argument, 0, 2},
                                           {"dest-ip", required_argument, 0, 3}, {"dest-port", required_argument, 0, 4},
                                           {"bitrate", required_argument, 0, 5}, {"pacing-pcr", no_argument, 0, 6},
                                           {"repair-cc", no_argument, 0, 7},     {"repair-pcr", no_argument, 0, 8},
                                           {"forensics", no_argument, 0, 9},     {"http", required_argument, 0, 10},
                                           {"help", no_argument, 0, 'h'},        {0, 0, 0, 0}};

    int opt;
    while ((opt = getopt_long(argc, argv, "h", long_options, NULL)) != -1) {
        switch (opt) {
            case 1:
                srt_in = optarg;
                break;
            case 2:
                udp_in_port = atoi(optarg);
                break;
            case 3:
                cfg.pacing.dest_ip = optarg;
                break;
            case 4:
                cfg.pacing.port = (uint16_t)atoi(optarg);
                break;
            case 5:
                cfg.pacing.bitrate = strtoull(optarg, NULL, 10);
                break;
            case 6:
                pacing_pcr = true;
                break;
            case 7:
                cfg.enable_null_substitution = true;
                break;
            case 8:
                cfg.enable_pcr_restamp = true;
                break;
            case 9:
                break;
            case 10:
                strncpy(g_http_url, optarg, sizeof(g_http_url) - 1);
                break;
            case 'h':
                print_usage(argv[0]);
                return 0;
        }
    }

    if (!srt_in && !udp_in_port) {
        tsa_error(TAG, "Error: Ingest source required (--srt-in or --udp-in)");
        return 1;
    }

    if (cfg.pacing.bitrate == 0) cfg.pacing.bitrate = 10000000;  // Default 10Mbps

    cfg.analysis_primary.is_live = true;
    cfg.enable_action_engine = (cfg.enable_null_substitution || cfg.enable_pcr_restamp || pacing_pcr);
    if (pacing_pcr) cfg.pacing.mode = TSPACER_MODE_PCR;

    g_gw = tsa_gateway_create(&cfg);
    if (!g_gw) {
        tsa_error(TAG, "Failed to create gateway");
        return 1;
    }

    signal(SIGINT, sig_handler);
    signal(SIGTERM, sig_handler);

    pthread_t tid;
    pthread_create(&tid, NULL, http_server_thread, (void*)g_http_url);
    pthread_detach(tid);

    uint8_t buf[65536];
    uint64_t last_snap_ns = get_now_ns();

    if (srt_in) {
        tsa_info(TAG, "Gateway: SRT Ingest %s -> UDP Egress %s:%d", srt_in,
                 cfg.pacing.dest_ip ? cfg.pacing.dest_ip : "NONE", cfg.pacing.port);
        ts_ingest_srt_t* ingest = ts_ingest_srt_create(srt_in);
        if (!ingest) return 1;
        while (g_keep_running) {
            int len = ts_ingest_srt_recv(ingest, buf, sizeof(buf));
            uint64_t now = get_now_ns();
            if (len > 0) {
                for (int i = 0; i + 188 <= len; i += 188) {
                    tsa_gateway_process(g_gw, buf + i, now);
                }
            } else
                usleep(100);
            if (now - last_snap_ns > 500000000ULL) {
                tsa_srt_stats_t srt_stats = {0};
                ts_ingest_srt_get_stats(ingest, &srt_stats);
                tsa_update_srt_stats(tsa_gateway_get_tsa_handle(g_gw), &srt_stats);
                tsa_commit_snapshot(tsa_gateway_get_tsa_handle(g_gw), now);
                tsa_render_dashboard(tsa_gateway_get_tsa_handle(g_gw));
                last_snap_ns = now;
            }
        }
        ts_ingest_srt_destroy(ingest);
    } else {
        tsa_info(TAG, "Gateway: UDP Ingest :%d -> UDP Egress %s:%d", udp_in_port,
                 cfg.pacing.dest_ip ? cfg.pacing.dest_ip : "NONE", cfg.pacing.port);
        ts_ingest_udp_t* ingest = ts_ingest_udp_create(NULL, (uint16_t)udp_in_port);
        if (!ingest) return 1;
        while (g_keep_running) {
            int len = ts_ingest_udp_recv(ingest, buf, sizeof(buf));
            uint64_t now = get_now_ns();
            if (len > 0) {
                for (int i = 0; i + 188 <= len; i += 188) {
                    tsa_gateway_process(g_gw, buf + i, now);
                }
            } else
                usleep(100);
            if (now - last_snap_ns > 500000000ULL) {
                tsa_commit_snapshot(tsa_gateway_get_tsa_handle(g_gw), now);
                tsa_render_dashboard(tsa_gateway_get_tsa_handle(g_gw));
                last_snap_ns = now;
            }
        }
        ts_ingest_udp_destroy(ingest);
    }

    tsa_gateway_destroy(g_gw);
    return 0;
}
