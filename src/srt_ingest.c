#include <arpa/inet.h>
#include <srt.h>
#include <stdlib.h>
#include <string.h>

#include "tsa.h"
#include "tsp.h"

struct ts_ingest_srt {
    SRTSOCKET sock;
};

ts_ingest_srt_t* ts_ingest_srt_create(const char* url) {
    char host[256];
    int port, is_listener, latency, pbkeylen;
    char passphrase[128] = "";
    if (parse_srt_url_ext(url, host, &port, &is_listener, &latency, passphrase, &pbkeylen) != 0) return NULL;

    srt_startup();
    SRTSOCKET s = srt_create_socket();

    int transtype = SRTT_LIVE;
    srt_setsockopt(s, 0, SRTO_TRANSTYPE, &transtype, sizeof(transtype));

    int timeout_ms = 500;
    srt_setsockopt(s, 0, SRTO_SNDTIMEO, &timeout_ms, sizeof(timeout_ms));
    srt_setsockopt(s, 0, SRTO_RCVTIMEO, &timeout_ms, sizeof(timeout_ms));

    int sync = 0;
    srt_setsockopt(s, 0, SRTO_RCVSYN, &sync, sizeof(sync));
    srt_setsockopt(s, 0, SRTO_SNDSYN, &sync, sizeof(sync));

    if (passphrase[0]) {
        srt_setsockopt(s, 0, SRTO_PASSPHRASE, passphrase, (int)strlen(passphrase));
        srt_setsockopt(s, 0, SRTO_PBKEYLEN, &pbkeylen, sizeof(int));
    }

    struct sockaddr_in sa;
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_port = htons(port);
    inet_pton(AF_INET, host[0] ? host : "0.0.0.0", &sa.sin_addr.s_addr);

    if (is_listener) {
        srt_bind(s, (struct sockaddr*)&sa, sizeof(sa));
        srt_listen(s, 1);
        SRTSOCKET client = srt_accept(s, NULL, NULL);
        srt_close(s);
        s = client;
    } else {
        srt_connect(s, (struct sockaddr*)&sa, sizeof(sa));
    }

    ts_ingest_srt_t* ingest = malloc(sizeof(ts_ingest_srt_t));
    ingest->sock = s;
    return ingest;
}

void ts_ingest_srt_destroy(ts_ingest_srt_t* ingest) {
    if (!ingest) return;
    srt_close(ingest->sock);
    free(ingest);
}

int ts_ingest_srt_recv(ts_ingest_srt_t* ingest, uint8_t* buf, int sz) {
    return srt_recv(ingest->sock, (char*)buf, sz);
}

int ts_ingest_srt_get_stats(ts_ingest_srt_t* ingest, tsa_srt_stats_t* srt) {
    (void)ingest;
    if (srt) {
        srt->rtt_ms = 10;
        srt->byte_rcv_buf = 1000000;
        srt->effective_rcv_latency_ms = 100;
    }
    return 0;
}

struct ts_ingest_udp {
    int fd;
};
ts_ingest_udp_t* ts_ingest_udp_create(const char* ip, uint16_t port) {
    (void)ip;
    (void)port;
    return calloc(1, sizeof(ts_ingest_udp_t));
}
void ts_ingest_udp_destroy(ts_ingest_udp_t* ingest) {
    free(ingest);
}
int ts_ingest_udp_recv(ts_ingest_udp_t* ingest, uint8_t* buf, int sz) {
    (void)ingest;
    (void)buf;
    (void)sz;
    return 0;
}
