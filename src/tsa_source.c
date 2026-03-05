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
    char filter_ip[64];
    int filter_port;
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
static int get_rtp_header_len(const uint8_t* p, int len) {
    if (len < 12) return 0;
    int size = 12;
    size += 4 * (p[0] & 0x0F); // CSRC count
    if (p[0] & 0x10) { // Extension bit
        if (len < size + 4) return size;
        size += 4 + 4 * ((p[size + 2] << 8) | p[size + 3]);
    }
    return size;
}

static void pcap_packet_callback(u_char *user, const struct pcap_pkthdr *h, const u_char *pkt) {
    tsa_source_t* src = (tsa_source_t*)user;
    int offset = 0;
    int link_type = pcap_datalink(src->handle.pcap_hdl);

    if (link_type == DLT_EN10MB) {
        offset = 14; 
        if (h->caplen < 14) return;
        uint16_t eth_type = ntohs(*(uint16_t *)(pkt + 12));
        if (eth_type == 0x8100) {
            if (h->caplen < 18) return;
            offset += 4; // Skip VLAN
        }
    } else if (link_type == DLT_NULL) {
        offset = 4;
    } else if (link_type == 113) { // DLT_LINUX_SLL
        offset = 16;
    } else {
        return; // Unsupported
    }

    if (h->caplen < offset + 20 + 8) return; 

    struct iphdr *ip = (struct iphdr *)(pkt + offset);
    if (ip->version != 4 || ip->protocol != IPPROTO_UDP) return;

    int ip_hdr_len = ip->ihl * 4;
    if (h->caplen < offset + ip_hdr_len + 8) return;

    struct udphdr *udp = (struct udphdr *)(pkt + offset + ip_hdr_len);
    uint8_t *payload = (uint8_t *)udp + 8;
    int payload_len = ntohs(udp->len) - 8;
    
    // Safety check for payload length vs capture length
    if (payload_len <= 0 || (offset + ip_hdr_len + 8 + payload_len) > (int)h->caplen) return;

    uint64_t now_ns = (uint64_t)h->ts.tv_sec * 1000000000ULL + h->ts.tv_usec * 1000ULL;

    // TS over UDP (Raw)
    if (payload_len % 188 == 0) {
        src->cbs.on_packets(src->user_data, payload, payload_len / 188, now_ns);
    } 
    // TS over RTP
    else {
        int rtp_len = get_rtp_header_len(payload, payload_len);
        if (rtp_len > 0 && rtp_len < payload_len && (payload_len - rtp_len) % 188 == 0) {
            src->cbs.on_packets(src->user_data, payload + rtp_len, (payload_len - rtp_len) / 188, now_ns);
        }
    }
}

static void* pcap_thread(void* arg) {
    tsa_source_t* src = (tsa_source_t*)arg;
    bool is_offline = strstr(src->url, ".pcap") != NULL;
    
    while (src->running) {
        int ret = pcap_dispatch(src->handle.pcap_hdl, 100, pcap_packet_callback, (u_char*)src);
        if (ret < 0) {
            if (ret == -1) {
                char err[512];
                snprintf(err, sizeof(err), "PCAP Error: %s", pcap_geterr(src->handle.pcap_hdl));
                src->cbs.on_status(src->user_data, -1, err);
            } else if (ret == -2) {
                src->cbs.on_status(src->user_data, 0, "PCAP breakloop");
            }
            break;
        }
        if (ret == 0) {
            if (is_offline) {
                src->cbs.on_status(src->user_data, 0, "EOF");
                break;
            }
            usleep(1000);
        }
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

tsa_source_t* tsa_source_create(tsa_source_type_t type, const char* url, const char* filter_ip, int filter_port, const tsa_source_callbacks_t* cbs, void* user_data) {
    tsa_source_t* src = calloc(1, sizeof(tsa_source_t));
    src->type = type;
    strncpy(src->url, url, sizeof(src->url)-1);
    if (filter_ip) strncpy(src->filter_ip, filter_ip, sizeof(src->filter_ip)-1);
    src->filter_port = filter_port;
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
        pthread_create(&src->thread, NULL, srt_thread, src);
    } else if (src->type == TSA_SOURCE_PCAP) {
#ifdef HAVE_PCAP
        char errbuf[PCAP_ERRBUF_SIZE];
        if (strstr(src->url, ".pcap")) {
            src->handle.pcap_hdl = pcap_open_offline(src->url, errbuf);
        } else {
            src->handle.pcap_hdl = pcap_create(src->url, errbuf);
            if (src->handle.pcap_hdl) {
                pcap_set_snaplen(src->handle.pcap_hdl, 65535);
                pcap_set_promisc(src->handle.pcap_hdl, 1);
                pcap_set_timeout(src->handle.pcap_hdl, 10);
                pcap_set_immediate_mode(src->handle.pcap_hdl, 1);
                pcap_set_buffer_size(src->handle.pcap_hdl, 16 * 1024 * 1024);
                if (pcap_activate(src->handle.pcap_hdl) != 0) {
                    src->cbs.on_status(src->user_data, -1, pcap_geterr(src->handle.pcap_hdl));
                    return -1;
                }
            }
        }
        if (!src->handle.pcap_hdl) {
            src->cbs.on_status(src->user_data, -1, errbuf);
            return -1;
        }

        // Professional BPF Filtering
        char bpf_buf[256] = "udp";
        if (src->filter_ip[0] && src->filter_port > 0) {
            snprintf(bpf_buf, sizeof(bpf_buf), "host %s and udp port %d", src->filter_ip, src->filter_port);
        } else if (src->filter_ip[0]) {
            snprintf(bpf_buf, sizeof(bpf_buf), "host %s and udp", src->filter_ip);
        } else if (src->filter_port > 0) {
            snprintf(bpf_buf, sizeof(bpf_buf), "udp port %d", src->filter_port);
        }

        struct bpf_program fp;
        if (pcap_compile(src->handle.pcap_hdl, &fp, bpf_buf, 1, PCAP_NETMASK_UNKNOWN) == 0) {
            pcap_setfilter(src->handle.pcap_hdl, &fp);
            pcap_freecode(&fp);
            printf("PCAP: BPF filter applied: '%s'\n", bpf_buf);
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
    if (src->thread) pthread_join(src->thread, NULL);
    return 0;
}
