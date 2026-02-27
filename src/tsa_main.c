#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <signal.h>
#include <pthread.h>
#include "tsa.h"
#include "../deps/mongoose/mongoose.h"

static tsa_handle_t* g_h = NULL;
static volatile int g_keep_running = 1;

// 引用新的工业级导出器
extern void tsa_exporter_prom_v2(tsa_handle_t** handles, int count, char* buf, size_t sz);

static void sig_handler(int sig) { (void)sig; g_keep_running = 0; }

static void fn(struct mg_connection* c, int ev, void* ev_data) {
    if (ev == MG_EV_HTTP_MSG) {
        struct mg_http_message* hm = (struct mg_http_message*)ev_data;
        if (mg_match(hm->uri, mg_str("/metrics"), NULL)) {
            static char resp[128 * 1024];
            tsa_exporter_prom_v2(&g_h, 1, resp, sizeof(resp));
            mg_http_reply(c, 200, "Content-Type: text/plain\r\n", "%s", resp);
        } else if (mg_match(hm->uri, mg_str("/api/v1/metrology/full"), NULL)) {
            static char resp[128 * 1024];
            static tsa_snapshot_full_t snap; // Move to static to avoid stack overflow
            tsa_take_snapshot_full(g_h, &snap);
            tsa_snapshot_to_json(&snap, resp, sizeof(resp));
            mg_http_reply(c, 200, "Content-Type: application/json\r\n", "%s", resp);
        }
    }
}

static void* http_thread(void* arg) {
    struct mg_mgr mgr; mg_mgr_init(&mgr);
    if (mg_http_listen(&mgr, "http://0.0.0.0:12345", fn, NULL) == NULL) {
        fprintf(stderr, "CLI Error: Failed to listen on http://0.0.0.0:12345\n");
        return NULL;
    }
    printf("CLI: Metrics server listening on http://localhost:12345/metrics\n");
    while (g_keep_running) mg_mgr_poll(&mgr, 100);
    mg_mgr_free(&mgr);
    return NULL;
}

int main(int argc, char** argv) {
    if (argc < 2) { printf("Usage: %s <test.ts>\n", argv[0]); return 1; }

    tsa_config_t cfg = { .is_live = false, .pcr_ema_alpha = 0.1 };
    strncpy(cfg.input_label, "CLI-TEST", sizeof(cfg.input_label));
    g_h = tsa_create(&cfg);

    signal(SIGINT, sig_handler);
    pthread_t tid; pthread_create(&tid, NULL, http_thread, NULL);

    FILE* f = fopen(argv[1], "rb");
    if (!f) { perror("fopen"); return 1; }

    uint8_t pkt[188];
    uint64_t count = 0;
    uint64_t now_ns = (uint64_t)time(NULL) * 1000000000ULL;

    printf("CLI: Analyzing %s...\n", argv[1]);
    while (g_keep_running && fread(pkt, 1, 188, f) == 188) {
        tsa_process_packet(g_h, pkt, now_ns);
        count++;
        now_ns += 188ULL * 8 * 1000000000 / 10000000; // 模拟 10Mbps 步进

        if (count % 1000 == 0) {
            tsa_commit_snapshot(g_h, now_ns);
            tsa_snapshot_full_t snap;
            tsa_take_snapshot_full(g_h, &snap);
            const tsa_tr101290_stats_t* s = &snap.stats;
            uint64_t p1_total = s->sync_loss.count + s->pat_error.count + s->cc_error.count +
                                s->pmt_error.count + s->pid_error.count;
            uint64_t p2_total = s->transport_error.count + s->crc_error.count +
                                s->pcr_repetition_error.count + s->pcr_accuracy_error.count;

            printf("\r[PKTS: %lu] BITRATE: %.2f Mbps | P1: %lu | P2: %lu | HEALTH: %.1f",
                   count, (double)s->physical_bitrate_bps / 1e6,
                   p1_total, p2_total, snap.predictive.master_health);
            fflush(stdout);
        }
    }

    tsa_commit_snapshot(g_h, now_ns);
    printf("\nCLI: Analysis Finished. Staying alive for metrics. Ctrl+C to exit.\n");
    fclose(f);
    while (g_keep_running) sleep(1);

    tsa_destroy(g_h);
    return 0;
}
