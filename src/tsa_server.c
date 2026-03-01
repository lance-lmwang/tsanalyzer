#define _GNU_SOURCE
#include <arpa/inet.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "../deps/mongoose/mongoose.h"
#include "tsa.h"
#include "tsa_internal.h"

extern void tsa_exporter_prom_v2(tsa_handle_t **handles, int count, char *buf, size_t sz);

#define MAX_STREAMS 16
#define HTTP_PORT "8080"

typedef struct {
    char id[32];
    int port;
    tsa_handle_t *tsa;
    pthread_t thread;
} node_t;

static node_t g_nodes[MAX_STREAMS];
static int g_node_count = 0;
static _Atomic int g_run = 1;

static void *worker(void *arg) {
    node_t *s = (node_t *)arg;
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    int rcvbuf = 16 * 1024 * 1024;
    setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &rcvbuf, sizeof(rcvbuf));

    struct sockaddr_in sa = {0};
    sa.sin_family = AF_INET;
    sa.sin_port = htons(s->port);
    sa.sin_addr.s_addr = INADDR_ANY;

    if (bind(fd, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
        perror("SRV: Bind failed");
        return NULL;
    }

    uint8_t buf[1500 * 7];
    uint64_t last_commit = (uint64_t)ts_now_ns128();

    while (atomic_load(&g_run)) {
        ssize_t len = recv(fd, buf, sizeof(buf), MSG_DONTWAIT);
        uint64_t now = (uint64_t)ts_now_ns128();
        if (len > 0) {
            for (int i = 0; i < (int)len / 188; i++) {
                tsa_process_packet(s->tsa, buf + (i * 188), now);
            }
        }
        if (now - last_commit > 1000000000ULL) {
            tsa_commit_snapshot(s->tsa, now);
            last_commit = now;
        }
        if (len <= 0) usleep(500);
    }
    close(fd);
    return NULL;
}

static void load_config(const char *file) {
    FILE *fp = fopen(file, "r");
    if (!fp) return;
    char line[512], id[32], url[256];
    while (fgets(line, sizeof(line), fp) && g_node_count < MAX_STREAMS) {
        if (line[0] == '#' || line[0] == '\n') continue;
        if (sscanf(line, "%s %s", id, url) == 2) {
            node_t *n = &g_nodes[g_node_count++];
            strncpy(n->id, id, 31);
            char *p_str = strrchr(url, ':');
            n->port = p_str ? atoi(p_str + 1) : (19001 + g_node_count);
            tsa_config_t cfg = {.is_live = true, .pcr_ema_alpha = 0.1};
            strncpy(cfg.input_label, id, 31);
            n->tsa = tsa_create(&cfg);
        }
    }
    fclose(fp);
}

static void fn(struct mg_connection *c, int ev, void *ev_data) {
    if (ev == MG_EV_HTTP_MSG) {
        struct mg_http_message *hm = (struct mg_http_message *)ev_data;
        if (mg_match(hm->uri, mg_str("/metrics"), NULL)) {
            static char resp[1024 * 1024];
            tsa_handle_t *h_list[MAX_STREAMS];
            for (int i = 0; i < g_node_count; i++) h_list[i] = g_nodes[i].tsa;
            tsa_exporter_prom_v2(h_list, g_node_count, resp, sizeof(resp));
            mg_http_reply(c, 200, "Content-Type: text/plain\r\nAccess-Control-Allow-Origin: *\r\n", "%s", resp);
        }
    }
}

int main(int argc, char **argv) {
    load_config((argc > 1) ? argv[1] : "tsa.conf");
    printf("SRV: TsAnalyzer Engine starting with %d nodes...\n", g_node_count);
    for (int i = 0; i < g_node_count; i++) {
        pthread_create(&g_nodes[i].thread, NULL, worker, &g_nodes[i]);
    }
    struct mg_mgr mgr;
    mg_mgr_init(&mgr);
    mg_http_listen(&mgr, "http://0.0.0.0:" HTTP_PORT, fn, NULL);
    while (atomic_load(&g_run)) mg_mgr_poll(&mgr, 100);
    mg_mgr_free(&mgr);
    return 0;
}
