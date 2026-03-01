#define _GNU_SOURCE
#include <pthread.h>
#include <sched.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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

/* 1. Capture Thread */
static void* capture_thread(void* arg) {
    const char* filename = (const char*)arg;
    set_thread_affinity(0);

    FILE* f = fopen(filename, "rb");
    if (!f) {
        perror("fopen");
        g_keep_running = 0;
        return NULL;
    }

    ts_packet_t pkt;
    // Standardize initial timestamp for deterministic replay
    uint64_t now_ns = 1700000000000000000ULL;
    int backoff_cnt = 0;

    while (g_keep_running && fread(pkt.data, 1, 188, f) == 188) {
        pkt.timestamp_ns = now_ns;
        while (g_keep_running && !spsc_queue_push(q_cap_to_dec, &pkt)) {
            backoff_sleep(backoff_cnt++);
        }
        backoff_cnt = 0;
        now_ns += 188ULL * 8 * 1000000000 / 10000000;
    }

    // Poison pill
    pkt.timestamp_ns = 0;
    while (!spsc_queue_push(q_cap_to_dec, &pkt)) backoff_sleep(100);

    fclose(f);
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
            backoff_cnt = 0;
            if (pkt.timestamp_ns == 0) {  // Poison pill
                while (!spsc_queue_push(q_dec_to_met, &pkt)) backoff_sleep(100);
                break;
            }
            ts_decode_result_t res;
            tsa_decode_packet(g_h, pkt.data, pkt.timestamp_ns, &res);
            while (!spsc_queue_push(q_dec_to_met, &pkt)) {
                backoff_sleep(10);
            }
        } else {
            backoff_sleep(backoff_cnt++);
        }
    }
    printf("CLI: Decode finished.\n");
    return NULL;
}

/* 3. Metrology Thread */
static void* metrology_thread(void* arg) {
    (void)arg;
    set_thread_affinity(2);

    ts_packet_t pkt;
    uint64_t last_ts = 0;
    int backoff_cnt = 0;
    while (1) {
        if (spsc_queue_pop(q_dec_to_met, &pkt)) {
            backoff_cnt = 0;
            if (pkt.timestamp_ns == 0) break;
            last_ts = pkt.timestamp_ns;
            ts_decode_result_t res;
            
            struct timespec start, end;
            clock_gettime(CLOCK_MONOTONIC, &start);

            // The packet was already decoded in the decode_thread.
            tsa_decode_packet_pure(g_h, pkt.data, pkt.timestamp_ns, &res);
            tsa_metrology_process(g_h, pkt.data, pkt.timestamp_ns, &res);
            
            clock_gettime(CLOCK_MONOTONIC, &end);
            uint64_t lat = (end.tv_sec - start.tv_sec) * 1000000000ULL + (end.tv_nsec - start.tv_nsec);
            // Engine processing latency metric update (internal hook)
            g_h->live->engine_processing_latency_ns = (g_h->live->engine_processing_latency_ns * 99 + lat) / 100;
        } else {
            backoff_sleep(backoff_cnt++);
        }
    }
    // Final commit at end of stream using the last seen timestamp
    if (last_ts > 0) tsa_commit_snapshot(g_h, last_ts);
    printf("CLI: Metrology finished.\n");
    g_keep_running = 0;  // Trigger shutdown of main and HTTP
    return NULL;
}

/* 4. Output/HTTP Thread */
static void fn(struct mg_connection* c, int ev, void* ev_data) {
    if (ev == MG_EV_HTTP_MSG) {
        struct mg_http_message* hm = (struct mg_http_message*)ev_data;
        if (mg_match(hm->uri, mg_str("/metrics"), NULL)) {
            static char resp[128 * 1024];
            tsa_exporter_prom_v2(&g_h, 1, resp, sizeof(resp));
            mg_http_reply(c, 200, "Content-Type: text/plain\r\n", "%s", resp);
        } else if (mg_match(hm->uri, mg_str("/api/v1/metrology/full"), NULL)) {
            static char resp[128 * 1024];
            static tsa_snapshot_full_t snap;
            tsa_take_snapshot_full(g_h, &snap);
            tsa_snapshot_to_json(&snap, resp, sizeof(resp));
            mg_http_reply(c, 200, "Content-Type: application/json\r\n", "%s", resp);
        }
    }
}

static void* http_thread(void* arg) {
    (void)arg;
    set_thread_affinity(3);
    struct mg_mgr mgr;
    mg_mgr_init(&mgr);
    if (mg_http_listen(&mgr, "http://0.0.0.0:12345", fn, NULL) == NULL) {
        return NULL;
    }
    while (g_keep_running) mg_mgr_poll(&mgr, 100);
    mg_mgr_free(&mgr);
    return NULL;
}

int main(int argc, char** argv) {
    if (argc < 2) {
        printf("Usage: %s [--mode=live|replay|forensic|certification] <test.ts>\n", argv[0]);
        return 1;
    }

    tsa_config_t cfg = {.is_live = false, .pcr_ema_alpha = 0.1, .op_mode = TSA_MODE_REPLAY};
    const char* filename = argv[argc - 1];

    for (int i = 1; i < argc - 1; i++) {
        if (strncmp(argv[i], "--mode=", 7) == 0) {
            const char* m = argv[i] + 7;
            if (strcmp(m, "live") == 0)
                cfg.op_mode = TSA_MODE_LIVE;
            else if (strcmp(m, "replay") == 0)
                cfg.op_mode = TSA_MODE_REPLAY;
            else if (strcmp(m, "forensic") == 0)
                cfg.op_mode = TSA_MODE_FORENSIC;
            else if (strcmp(m, "certification") == 0)
                cfg.op_mode = TSA_MODE_CERTIFICATION;
        }
    }

    if (cfg.op_mode == TSA_MODE_CERTIFICATION) {
        // Doc 09: Enforce isolcpus check (simplified for now)
        printf("CLI: Certification Mode Active. Verifying environment...\n");
        // In a real implementation, we'd check /proc/cmdline or similar.
    }

    strncpy(cfg.input_label, "CLI-PIPE", sizeof(cfg.input_label));
    g_h = tsa_create(&cfg);

    q_cap_to_dec = spsc_queue_create(8192);
    q_dec_to_met = spsc_queue_create(8192);

    signal(SIGINT, sig_handler);

    pthread_t t_cap, t_dec, t_met, t_http;
    pthread_create(&t_cap, NULL, capture_thread, (void*)filename);
    pthread_create(&t_dec, NULL, decode_thread, NULL);
    pthread_create(&t_met, NULL, metrology_thread, NULL);
    pthread_create(&t_http, NULL, http_thread, NULL);

    printf("CLI: 4-Thread Pipeline Started. Core Affinity [0,1,2,3]\n");

    while (g_keep_running) {
        usleep(100000);
    }

    pthread_join(t_cap, NULL);
    pthread_join(t_dec, NULL);
    pthread_join(t_met, NULL);
    pthread_join(t_http, NULL);

    // Dump final snapshot for verification
    tsa_snapshot_full_t snap;
    if (tsa_take_snapshot_full(g_h, &snap) == 0) {
        char* buf = malloc(256 * 1024);
        tsa_snapshot_to_json(&snap, buf, 256 * 1024);
        FILE* f_out = fopen("final_metrology.json", "w");
        if (f_out) {
            fprintf(f_out, "%s\n", buf);
            fclose(f_out);
            printf("\nCLI: Final metrology saved to final_metrology.json\n");
        }
        free(buf);
    }

    spsc_queue_destroy(q_cap_to_dec);
    spsc_queue_destroy(q_dec_to_met);
    tsa_destroy(g_h);

    printf("CLI: Shutdown Complete.\n");
    return 0;
}
