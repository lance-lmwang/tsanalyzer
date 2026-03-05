#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <errno.h>
#include <srt.h>

#ifdef HAVE_PCAP
#include <pcap.h>
#include <netinet/ip.h>
#include <netinet/udp.h>
#include <netinet/if_ether.h>
#endif

#include "tsa_source.h"
#include "tsa.h"

struct tsa_source {
    tsa_source_type_t type;
    char url[256];
    tsa_source_callbacks_t cbs;
    void* user_data;
    
    pthread_t thread;
    volatile bool running;
    
    union {
        int udp_fd;
        SRTSOCKET srt_sock;
        FILE* fp;
#ifdef HAVE_PCAP
        pcap_t* pcap_hdl;
#endif
    } handle;
};

#ifdef HAVE_PCAP
static void pcap_packet_callback(u_char *user, const struct pcap_pkthdr *h, const u_char *pkt) {
    tsa_source_t* src = (tsa_source_t*)user;
    
    // Simple Ethernet/IP/UDP decapsulation
    if (h->caplen < 14 + 20 + 8) return; 

    struct ethhdr *eth = (struct ethhdr *)pkt;
    if (ntohs(eth->h_proto) != ETH_P_IP) return;

    struct iphdr *ip = (struct iphdr *)(pkt + 14);
    if (ip->protocol != IPPROTO_UDP) return;

    int ip_hdr_len = ip->ihl * 4;
    struct udphdr *udp = (struct udphdr *)(pkt + 14 + ip_hdr_len);
    uint8_t *payload = (uint8_t *)udp + 8;
    int payload_len = ntohs(udp->len) - 8;

    if (payload_len > 0 && payload_len % 188 == 0) {
        uint64_t now_ns = (uint64_t)h->ts.tv_sec * 1000000000ULL + h->ts.tv_usec * 1000ULL;
        src->cbs.on_packets(src->user_data, payload, payload_len / 188, now_ns);
    } else if (payload_len > 12 && (payload_len - 12) % 188 == 0) {
        /* RTP Decapsulation */
        uint64_t now_ns = (uint64_t)h->ts.tv_sec * 1000000000ULL + h->ts.tv_usec * 1000ULL;
        src->cbs.on_packets(src->user_data, payload + 12, (payload_len - 12) / 188, now_ns);
    }
}

static void* pcap_thread(void* arg) {
    tsa_source_t* src = (tsa_source_t*)arg;
    while (src->running) {
        int ret = pcap_dispatch(src->handle.pcap_hdl, 100, pcap_packet_callback, (u_char*)src);
        if (ret < 0) {
            src->cbs.on_status(src->user_data, -1, "PCAP error or EOF");
            break;
        }
        if (ret == 0) usleep(1000);
    }
    src->cbs.on_packets(src->user_data, NULL, 0, 0); // Poison pill
    return NULL;
}
#endif

static void* file_thread(void* arg) {
    tsa_source_t* src = (tsa_source_t*)arg;
    uint8_t buf[188 * 7];
    while (src->running) {
        size_t n = fread(buf, 1, sizeof(buf), src->handle.fp);
        if (n > 0) {
            src->cbs.on_packets(src->user_data, buf, n / 188, 0);
        } else {
            if (feof(src->handle.fp)) {
                src->cbs.on_status(src->user_data, 0, "EOF");
            } else {
                src->cbs.on_status(src->user_data, -1, "File read error");
            }
            break;
        }
    }
    src->cbs.on_packets(src->user_data, NULL, 0, 0); // Poison pill
    return NULL;
}

static void* udp_thread(void* arg) {
    tsa_source_t* src = (tsa_source_t*)arg;
    uint8_t buf[1504];
    while (src->running) {
        ssize_t n = recv(src->handle.udp_fd, buf, sizeof(buf), 0);
        if (n > 0) {
            uint64_t now = 0; 
            int count = n / 188;
            if (count > 0) {
                src->cbs.on_packets(src->user_data, buf, count, now);
            }
        } else if (n < 0 && (errno != EAGAIN && errno != EWOULDBLOCK)) {
            src->cbs.on_status(src->user_data, -1, "UDP receive error");
            break;
        }
        if (n <= 0) usleep(1000);
    }
    src->cbs.on_packets(src->user_data, NULL, 0, 0); // Poison pill
    return NULL;
}

static void* srt_thread(void* arg) {
    tsa_source_t* src = (tsa_source_t*)arg;
    uint8_t buf[1316 * 7];
    while (src->running) {
        int n = srt_recv(src->handle.srt_sock, (char*)buf, sizeof(buf));
        if (n > 0) {
            uint64_t now = 0;
            int count = n / 188;
            if (count > 0) {
                src->cbs.on_packets(src->user_data, buf, count, now);
            }
        } else if (n == SRT_ERROR && srt_getlasterror(NULL) != SRT_EASYNCRCV) {
            src->cbs.on_status(src->user_data, -1, "SRT receive error");
            break;
        }
        if (n <= 0) usleep(1000);
    }
    src->cbs.on_packets(src->user_data, NULL, 0, 0); // Poison pill
    return NULL;
}

tsa_source_t* tsa_source_create(tsa_source_type_t type, const char* url, const tsa_source_callbacks_t* cbs, void* user_data) {
    tsa_source_t* src = calloc(1, sizeof(tsa_source_t));
    src->type = type;
    strncpy(src->url, url, sizeof(src->url)-1);
    src->cbs = *cbs;
    src->user_data = user_data;
    return src;
}

void tsa_source_destroy(tsa_source_t* src) {
    if (!src) return;
    tsa_source_stop(src);
    if (src->type == TSA_SOURCE_UDP) {
        close(src->handle.udp_fd);
    } else if (src->type == TSA_SOURCE_SRT) {
        srt_close(src->handle.srt_sock);
#ifdef HAVE_PCAP
    } else if (src->type == TSA_SOURCE_PCAP) {
        pcap_close(src->handle.pcap_hdl);
#endif
    } else if (src->type == TSA_SOURCE_FILE) {
        if (src->handle.fp) {
            fclose(src->handle.fp);
            src->handle.fp = NULL;
        }
    }
    free(src);
}

int tsa_source_start(tsa_source_t* src) {
    src->running = true;
    if (src->type == TSA_SOURCE_UDP) {
        char* p = strrchr(src->url, ':');
        int port = p ? atoi(p + 1) : 1234;
        src->handle.udp_fd = socket(AF_INET, SOCK_DGRAM, 0);
        struct sockaddr_in sa = {0};
        sa.sin_family = AF_INET;
        sa.sin_addr.s_addr = INADDR_ANY;
        sa.sin_port = htons(port);
        bind(src->handle.udp_fd, (struct sockaddr*)&sa, sizeof(sa));
        fcntl(src->handle.udp_fd, F_SETFL, O_NONBLOCK);
        pthread_create(&src->thread, NULL, udp_thread, src);
    } else if (src->type == TSA_SOURCE_SRT) {
        srt_startup();
        src->handle.srt_sock = srt_create_socket();
        // Simplified SRT init for example
        pthread_create(&src->thread, NULL, srt_thread, src);
    } else if (src->type == TSA_SOURCE_PCAP) {
#ifdef HAVE_PCAP
        char errbuf[PCAP_ERRBUF_SIZE];
        if (strstr(src->url, ".pcap")) {
            src->handle.pcap_hdl = pcap_open_offline(src->url, errbuf);
        } else {
            src->handle.pcap_hdl = pcap_open_live(src->url, 65535, 1, 100, errbuf);
        }
        if (!src->handle.pcap_hdl) {
            src->cbs.on_status(src->user_data, -1, errbuf);
            return -1;
        }
        pthread_create(&src->thread, NULL, pcap_thread, src);
#else
        src->cbs.on_status(src->user_data, -1, "PCAP support not compiled in");
        return -1;
#endif
    } else if (src->type == TSA_SOURCE_FILE) {
        src->handle.fp = fopen(src->url, "rb");
        if (!src->handle.fp) {
            src->cbs.on_status(src->user_data, -1, "Failed to open file");
            return -1;
        }
        pthread_create(&src->thread, NULL, file_thread, src);
    }
    return 0;
}

int tsa_source_stop(tsa_source_t* src) {
    src->running = false;
    pthread_join(src->thread, NULL);
    return 0;
}
