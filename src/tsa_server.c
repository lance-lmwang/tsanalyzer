#define _GNU_SOURCE
#include <arpa/inet.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "mongoose.h"
#include "tsa.h"
#include "tsa_conf.h"
#include "tsa_internal.h"
#include "tsa_log.h"

#define TAG "SERVER"

extern void tsa_exporter_prom_v2(tsa_handle_t **handles, int count, char *buf, size_t sz);

#define MAX_STREAMS 16
static int g_http_port = 8088;
static char g_webhook_url[256] = "";
static uint64_t g_alert_mask = 0;

typedef struct {
    char id[32];
    int port;
    tsa_handle_t *tsa;
    pthread_t thread;
    bool active;
} node_t;

static node_t g_nodes[MAX_STREAMS];
static int g_node_count = 0;
static _Atomic int g_run = 1;
static pthread_mutex_t g_nodes_lock = PTHREAD_MUTEX_INITIALIZER;

static void *worker(void *arg) {
    node_t *s = (node_t *)arg;
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    int rcvbuf = 16 * 1024 * 1024;
    setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &rcvbuf, sizeof(rcvbuf));
    struct sockaddr_in sa = {0};
    sa.sin_family = AF_INET;
    sa.sin_port = htons(s->port);
    sa.sin_addr.s_addr = INADDR_ANY;
    if (bind(fd, (struct sockaddr *)&sa, sizeof(sa)) < 0) return NULL;

    uint8_t buf[1500 * 7];
    uint64_t last_commit = (uint64_t)ts_now_ns128();
    while (atomic_load(&g_run) && s->active) {
        ssize_t len = recv(fd, buf, sizeof(buf), MSG_DONTWAIT);
        uint64_t now = (uint64_t)ts_now_ns128();
        if (len > 0) {
            uint8_t *p = buf;
            int remaining = (int)len;
            while (remaining >= 188) {
                tsa_process_packet(s->tsa, p, now);
                p += 188;
                remaining -= 188;
            }
        }
        if (now - last_commit > 100000000ULL) {
            tsa_commit_snapshot(s->tsa, now);
            last_commit = now;
        }
        if (len <= 0) usleep(1000);
    }
    close(fd);
    return NULL;
}

static tsa_full_conf_t g_sys_conf;

static void apply_sys_conf() {
    g_http_port = g_sys_conf.http_listen_port > 0 ? g_sys_conf.http_listen_port : 8088;

    pthread_mutex_lock(&g_nodes_lock);
    for (int i = 0; i < g_sys_conf.stream_count; i++) {
        if (g_node_count >= MAX_STREAMS) break;
        node_t *n = &g_nodes[g_node_count++];
        strncpy(n->id, g_sys_conf.streams[i].id, 31);
        n->id[31] = '\0';

        char *p_str = strrchr(g_sys_conf.streams[i].cfg.url, ':');
        n->port = p_str ? atoi(p_str + 1) : (19001 + g_node_count);
        n->active = true;

        n->tsa = tsa_create(&g_sys_conf.streams[i].cfg);
        pthread_create(&n->thread, NULL, worker, n);
    }
    pthread_mutex_unlock(&g_nodes_lock);
}

static void load_config(const char *file) {
    if (tsa_conf_load(&g_sys_conf, file) == 0) {
        apply_sys_conf();
    } else {
        tsa_error(TAG, "Failed to load configuration file: %s", file);
    }
}

static void save_state() {
    FILE *fp = fopen("tsa_state.json", "w");
    if (!fp) return;
    fprintf(fp, "{\n  \"streams\": [\n");
    pthread_mutex_lock(&g_nodes_lock);
    bool first = true;
    for (int i = 0; i < g_node_count; i++) {
        if (!g_nodes[i].active) continue;
        if (!first) fprintf(fp, ",\n");
        fprintf(fp, "    {\"id\": \"%s\", \"url\": \"%s\", \"alert_mask\": %llu, \"webhook_url\": \"%s\"}",
                g_nodes[i].id, g_nodes[i].tsa->config.url, (unsigned long long)g_nodes[i].tsa->config.alert.filter_mask,
                g_nodes[i].tsa->config.alert.webhook_url);
        first = false;
    }
    pthread_mutex_unlock(&g_nodes_lock);
    fprintf(fp, "\n  ]\n}\n");
    fclose(fp);
}

static void fn(struct mg_connection *c, int ev, void *ev_data) {
    if (ev != MG_EV_HTTP_MSG) return;
    struct mg_http_message *hm = (struct mg_http_message *)ev_data;

    if (mg_match(hm->uri, mg_str("/metrics"), NULL)) {
        static char resp[1024 * 1024];
        tsa_handle_t *h_list[MAX_STREAMS];
        int count = 0;
        pthread_mutex_lock(&g_nodes_lock);
        for (int i = 0; i < g_node_count; i++)
            if (g_nodes[i].active) h_list[count++] = g_nodes[i].tsa;
        tsa_exporter_prom_v2(h_list, count, resp, sizeof(resp));
        pthread_mutex_unlock(&g_nodes_lock);
        mg_http_reply(c, 200, "Content-Type: text/plain\r\n", "%s", resp);
    } else if (mg_match(hm->uri, mg_str("/api/v1/streams"), NULL)) {
        if (mg_strcasecmp(hm->method, mg_str("POST")) == 0) {
            printf("POST /api/v1/streams received, body: %.*s\n", (int)hm->body.len, hm->body.buf);
            char id[64] = "";
            char webhook[256] = "";
            uint64_t mask = g_alert_mask;

            char *id_p = mg_json_get_str(hm->body, "$.id");
            if (id_p) {
                strncpy(id, id_p, 31);
                id[31] = '\0';
                free(id_p);
            }

            char *wh_p = mg_json_get_str(hm->body, "$.webhook_url");
            if (wh_p) {
                strncpy(webhook, wh_p, 255);
                webhook[255] = '\0';
                free(wh_p);
            } else
                strncpy(webhook, g_webhook_url, 255);

            char *mask_p = mg_json_get_str(hm->body, "$.alert_mask");
            if (mask_p) {
                mask = strtoull(mask_p, NULL, 0);
                free(mask_p);
            }

            pthread_mutex_lock(&g_nodes_lock);
            if (g_node_count < MAX_STREAMS) {
                node_t *n = &g_nodes[g_node_count++];
                strncpy(n->id, id[0] ? id : "dynamic", 31);
                n->id[31] = '\0';
                n->port = 19000 + g_node_count;
                n->active = true;

                tsa_config_t cfg = g_sys_conf.vhost_default;
                cfg.op_mode = TSA_MODE_LIVE;
                strncpy(cfg.input_label, n->id, 31);
                strncpy(cfg.alert.webhook_url, webhook, sizeof(cfg.alert.webhook_url) - 1);
                cfg.alert.filter_mask = mask;
                n->tsa = tsa_create(&cfg);
                pthread_create(&n->thread, NULL, worker, n);
                pthread_mutex_unlock(&g_nodes_lock);  // Unlock before calling save_state which re-locks
                save_state();
                printf("Stream created: %s on port %d\n", n->id, n->port);
                fflush(stdout);
                mg_http_reply(c, 200, "Content-Type: application/json\r\n", "{\"status\":\"ok\",\"port\":%d}", n->port);
            } else {
                pthread_mutex_unlock(&g_nodes_lock);
                printf("Stream creation failed: Full\n");
                fflush(stdout);
                mg_http_reply(c, 507, "", "Full");
            }
        } else {
            char *list_buf = calloc(1, 4096);
            int off = snprintf(list_buf, 4096, "{\"streams\":[");
            pthread_mutex_lock(&g_nodes_lock);
            int added = 0;
            for (int i = 0; i < g_node_count; i++) {
                if (g_nodes[i].active) {
                    off += snprintf(list_buf + off, 4096 - off, "%s\"%s\"", (added > 0 ? "," : ""), g_nodes[i].id);
                    added++;
                }
            }
            pthread_mutex_unlock(&g_nodes_lock);
            snprintf(list_buf + off, 4096 - off, "]}");
            mg_http_reply(c, 200, "Content-Type: application/json\r\n", "%s", list_buf);
            free(list_buf);
        }
    } else if (mg_match(hm->uri, mg_str("/api/v1/streams/*"), NULL)) {
        if (mg_strcasecmp(hm->method, mg_str("DELETE")) == 0) {
            char sid[64] = "";
            int id_len = (int)(hm->uri.len - 16);
            if (id_len > 0 && id_len < 63) {
                memcpy(sid, hm->uri.buf + 16, id_len);
                sid[id_len] = '\0';
                pthread_mutex_lock(&g_nodes_lock);
                for (int i = 0; i < g_node_count; i++)
                    if (strcmp(g_nodes[i].id, sid) == 0) g_nodes[i].active = false;
                pthread_mutex_unlock(&g_nodes_lock);
            }
            mg_http_reply(c, 200, "Content-Type: application/json\r\n", "{\"status\":\"ok\"}");
        } else
            mg_http_reply(c, 405, "", "Method Not Allowed");
    } else if (mg_match(hm->uri, mg_str("/api/v1/snapshot"), NULL)) {
        char *snap_buf = calloc(1, 1024 * 1024);
        int off = snprintf(snap_buf, 1024 * 1024, "[");
        static char stream_json[128 * 1024];
        pthread_mutex_lock(&g_nodes_lock);
        int added = 0;
        for (int i = 0; i < g_node_count; i++) {
            if (!g_nodes[i].active) continue;
            tsa_snapshot_full_t snap;
            if (tsa_take_snapshot_full(g_nodes[i].tsa, &snap) == 0) {
                tsa_snapshot_to_json(g_nodes[i].tsa, &snap, stream_json, sizeof(stream_json));
                off += snprintf(snap_buf + off, 1024 * 1024 - off, "%s%s", (added > 0 ? "," : ""), stream_json);
                added++;
            }
        }
        pthread_mutex_unlock(&g_nodes_lock);
        snprintf(snap_buf + off, 1024 * 1024 - off, "]");
        mg_http_reply(c, 200, "Content-Type: application/json\r\n", "%s", snap_buf);
        free(snap_buf);
    } else {
        mg_http_reply(c, 404, "", "Not Found");
    }
}

int main(int argc, char **argv) {
    if (argc > 1) load_config(argv[1]);
    struct mg_mgr mgr;
    mg_mgr_init(&mgr);
    char addr[128];
    snprintf(addr, sizeof(addr), "http://0.0.0.0:%d", g_http_port);
    if (mg_http_listen(&mgr, addr, fn, NULL) == NULL) return 1;
    tsa_info(TAG, "OK: HTTP Server is now active at %s/metrics", addr);
    while (atomic_load(&g_run)) mg_mgr_poll(&mgr, 100);
    mg_mgr_free(&mgr);
    return 0;
}
