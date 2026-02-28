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

            // Find primary video and stuffing info for CLI display
            uint32_t w = 0, h = 0, gop = 0, g_min = 0, g_max = 0;
            uint64_t v_br = 0, v_min = 0, v_max = 0;
            double stuffing_pct = 0;
            const char* profile = "";

            for (int p=0; p<TS_PID_MAX; p++) {
                if (snap.pids[p].width > 0 && w == 0) {
                    w = snap.pids[p].width;
                    h = snap.pids[p].height;
                    gop = snap.pids[p].gop_n;
                    g_min = snap.pids[p].gop_min;
                    g_max = snap.pids[p].gop_max;
                    v_br = (uint64_t)((double)snap.pids[p].bitrate_q16_16 / 65536.0);
                    v_min = snap.pids[p].bitrate_min;
                    v_max = snap.pids[p].bitrate_max;
                    if (snap.pids[p].profile == 100) profile = "High";
                    else if (snap.pids[p].profile == 77) profile = "Main";
                    else if (snap.pids[p].profile == 66) profile = "Baseline";
                }
                if (p == 0x1FFF) {
                    stuffing_pct = (s->physical_bitrate_bps > 0) ?
                                   (double)s->pid_bitrate_bps[p] * 100.0 / (double)s->physical_bitrate_bps : 0;
                }
            }

            printf("\r[PKTS: %lu] BITRATE: %.2f Mbps (Null: %.1f%%) | P1: %lu | VIDEO: %ux%u | V-BR: %.2f (%.2f-%.2f) Mbps | GOP: %u (%u-%u) | HEALTH: %.1f",
                   count, (double)s->physical_bitrate_bps / 1e6, stuffing_pct,
                   p1_total, w, h, (double)v_br / 1e6, (double)v_min / 1e6, (double)v_max / 1e6,
                   gop, g_min, g_max, snap.predictive.master_health);
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
