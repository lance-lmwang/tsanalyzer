#define _GNU_SOURCE
#include <arpa/inet.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <unistd.h>

#include "../deps/mongoose/mongoose.h"
#include "tsa.h"

extern void tsa_exporter_prom_v2(tsa_handle_t **handles, int count, char *buf, size_t sz);

#define MAX_STREAMS 8
#define HTTP_PORT "8080"

typedef struct {
    int port;
    tsa_handle_t *tsa;
    pthread_t thread;
    _Atomic uint64_t pkts;
} node_t;
static node_t g_nodes[MAX_STREAMS];
static _Atomic int g_run = 1;

static void *worker(void *arg) {
    node_t *s = (node_t *)arg;
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in sa = {.sin_family = AF_INET, .sin_port = htons(s->port), .sin_addr.s_addr = INADDR_ANY};
    bind(fd, (struct sockaddr *)&sa, sizeof(sa));
    uint8_t buf[1500 * 7];
    while (atomic_load(&g_run)) {
        ssize_t len = recv(fd, buf, sizeof(buf), MSG_DONTWAIT);
        if (len > 0) {
            uint64_t now = ts_now_ns128();
            for (int i = 0; i < (int)len / 188; i++) tsa_process_packet(s->tsa, buf + (i * 188), now);
            tsa_commit_snapshot(s->tsa, now);
        } else {
            usleep(500);
        }
    }
    close(fd);
    return NULL;
}

static void fn(struct mg_connection *c, int ev, void *ev_data) {
    if (ev == MG_EV_HTTP_MSG) {
        struct mg_http_message *hm = (struct mg_http_message *)ev_data;
        if (mg_match(hm->uri, mg_str("/metrics"), NULL)) {
            static char resp[1024 * 1024];
            tsa_handle_t *h_list[MAX_STREAMS];
            for (int i = 0; i < MAX_STREAMS; i++) h_list[i] = g_nodes[i].tsa;
            tsa_exporter_prom_v2(h_list, MAX_STREAMS, resp, sizeof(resp));
            mg_http_reply(c, 200, "Content-Type: text/plain\r\n", "%s", resp);
        }
    }
}

int main() {
    for (int i = 0; i < MAX_STREAMS; i++) {
        tsa_config_t cfg = {.is_live = true};
        sprintf(cfg.input_label, "STR-%d", i + 1);
        g_nodes[i].port = 19001 + i;
        g_nodes[i].tsa = tsa_create(&cfg);
        pthread_create(&g_nodes[i].thread, NULL, worker, &g_nodes[i]);
    }
    struct mg_mgr mgr;
    mg_mgr_init(&mgr);
    mg_http_listen(&mgr, "http://0.0.0.0:" HTTP_PORT, fn, NULL);
    while (atomic_load(&g_run)) mg_mgr_poll(&mgr, 100);
    return 0;
}
