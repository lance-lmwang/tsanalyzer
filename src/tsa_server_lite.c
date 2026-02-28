#include <arpa/inet.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "mongoose.h"

#define MAX_S 8
typedef struct {
    uint64_t pkts;
    uint64_t last_pkts;
    uint64_t br;
    int fd;
    char id[16];
} stream_t;

static stream_t g_s[MAX_S];
static struct timespec g_last_time;
static int g_keep_running = 1;

static void sig_handler(int sig) {
    (void)sig;
    g_keep_running = 0;
}

static void fn(struct mg_connection *c, int ev, void *ev_data) {
    if (ev == MG_EV_HTTP_MSG) {
        struct mg_http_message *hm = (struct mg_http_message *)ev_data;
        if (mg_match(hm->uri, mg_str("/metrics"), NULL)) {
            char buf[4096];
            int off = 0;
            for (int i = 0; i < MAX_S; i++) {
                off += snprintf(buf + off, sizeof(buf) - off,
                                "tsa_total_packets{mode=\"%s\"} %llu\n"
                                "tsa_physical_bitrate_bps{mode=\"%s\"} %llu\n",
                                g_s[i].id, (unsigned long long)g_s[i].pkts, g_s[i].id, (unsigned long long)g_s[i].br);
            }
            mg_http_reply(c, 200, "Content-Type: text/plain\r\n", "%s", buf);
        } else {
            mg_http_reply(c, 200, "", "TSA PRO STABLE V5\n");
        }
    }
}

int main() {
    struct mg_mgr mgr;
    mg_mgr_init(&mgr);
    if (mg_http_listen(&mgr, "http://0.0.0.0:8080", fn, NULL) == NULL) return 1;

    for (int i = 0; i < MAX_S; i++) {
        sprintf(g_s[i].id, "S%d", i + 1);
        g_s[i].fd = socket(AF_INET, SOCK_DGRAM, 0);
        struct sockaddr_in sa;
        memset(&sa, 0, sizeof(sa));
        sa.sin_family = AF_INET;
        sa.sin_port = htons(19001 + i);
        int r = 1;
        setsockopt(g_s[i].fd, SOL_SOCKET, SO_REUSEADDR, &r, sizeof(r));
        bind(g_s[i].fd, (struct sockaddr *)&sa, sizeof(sa));
    }

    clock_gettime(CLOCK_MONOTONIC, &g_last_time);
    signal(SIGINT, sig_handler);
    printf("STABLE ENGINE STARTING (8 PORTS)...\n");

    uint8_t pkt[2048];
    while (g_keep_running) {
        mg_mgr_poll(&mgr, 1);

        uint64_t now_ns = 0;
        for (int i = 0; i < MAX_S; i++) {
            while (1) {
                ssize_t n = recv(g_s[i].fd, pkt, sizeof(pkt), MSG_DONTWAIT);
                if (n <= 0) break;
                for (ssize_t j = 0; j + 188 <= n; j += 188) {
                    if (pkt[j] == 0x47) g_s[i].pkts++;
                }
            }
        }

        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        double ds = (now.tv_sec - g_last_time.tv_sec) + (now.tv_nsec - g_last_time.tv_nsec) / 1e9;
        if (ds > 2.0) {  // 2s window for stability
            for (int i = 0; i < MAX_S; i++) {
                g_s[i].br = (uint64_t)(((g_s[i].pkts - g_s[i].last_pkts) * 188.0 * 8.0) / ds);
                g_s[i].last_pkts = g_s[i].pkts;
            }
            g_last_time = now;
        }
        usleep(1000);  // 1ms sleep to save OS quota
    }
    mg_mgr_free(&mgr);
    return 0;
}
