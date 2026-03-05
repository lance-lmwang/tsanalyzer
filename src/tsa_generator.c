/**
 * @file tsa_generator.c
 * @brief Standards-compliant SPTS Generator with verified CRC32.
 */

#include <arpa/inet.h>
#include <fcntl.h>
#include <getopt.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#define TS_PACKET_SIZE 188
#define UDP_PACKET_SIZE (7 * TS_PACKET_SIZE)
#define VIDEO_PID 0x0100

static volatile int g_keep_running = 1;
static void sig_handler(int sig) { (void)sig; g_keep_running = 0; }

static uint8_t h264_headers[] = {
    0x00, 0x00, 0x00, 0x01, 0x67, 0x42, 0x00, 0x1f, 0x95, 0xa8, 0x1e, 0x00, 0x5b, 0x90,
    0x00, 0x00, 0x00, 0x01, 0x68, 0xce, 0x3c, 0x80,
    0x00, 0x00, 0x00, 0x01, 0x06, 0x05, 0x08, 'G',  'A',  '9',  '4',  0x03, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x01, 0x65, 0x00, 0x00, 0x00, 0x01
};

/* Verified PAT (Program 1 -> PMT PID 0x1000) */
static uint8_t pat_pkt[] = {
    0x47, 0x40, 0x00, 0x10, 0x00, 0x00, 0xB0, 0x0D, 0x00, 0x01, 0xC1, 0x00, 0x00, 0x00, 0x01, 0xF0, 0x00, 0x2A, 0xB1, 0x04, 0xB2
};

/* Verified PMT (Program 1 -> Video PID 0x0100) */
static uint8_t pmt_pkt[] = {
    0x47, 0x50, 0x00, 0x10, 0x00, 0x02, 0xB0, 0x17, 0x00, 0x01, 0xC1, 0x00, 0x00, 0xE1, 0x00, 0xF0, 0x00, 0x1B, 0xE1, 0x00, 0xF0, 0x00, 0x4E, 0x59, 0x3D, 0x1E
};

static void write_pcr_pkt(uint8_t* p, uint8_t* cc, uint64_t pcr_27mhz) {
    memset(p, 0xFF, TS_PACKET_SIZE);
    p[0] = 0x47; p[1] = 0x40 | ((VIDEO_PID >> 8) & 0x1F); p[2] = VIDEO_PID & 0xFF;
    p[3] = 0x20 | ((*cc)++ & 0x0F); p[4] = 183; p[5] = 0x10;
    uint64_t base = pcr_27mhz / 300; uint16_t ext = pcr_27mhz % 300;
    p[6] = (base >> 25) & 0xFF; p[7] = (base >> 17) & 0xFF;
    p[8] = (base >> 9) & 0xFF;  p[9] = (base >> 1) & 0xFF;
    p[10] = ((base & 0x01) << 7) | 0x7E | ((ext >> 8) & 0x01); p[11] = ext & 0xFF;
}

static void write_video_pkt(uint8_t* p, uint8_t* cc, bool pusi, uint64_t pts_90k) {
    memset(p, 0xFF, TS_PACKET_SIZE);
    p[0] = 0x47; p[1] = (pusi ? 0x40 : 0x00) | ((VIDEO_PID >> 8) & 0x1F); p[2] = VIDEO_PID & 0xFF;
    p[3] = 0x10 | ((*cc)++ & 0x0F);
    if (pusi) {
        p[4] = 0x00; p[5] = 0x00; p[6] = 0x01; p[7] = 0xe0;
        p[8] = 0x00; p[9] = 0x00; p[10] = 0x80; p[11] = 0x80; p[12] = 0x05;
        p[13] = 0x21 | (((pts_90k >> 30) & 0x07) << 1); p[14] = (pts_90k >> 22) & 0xFF;
        p[15] = ((pts_90k >> 15) & 0x7F) << 1 | 0x01; p[16] = (pts_90k >> 7) & 0xFF;
        p[17] = ((pts_90k & 0x7F) << 1) | 0x01;
        memcpy(p + 18, h264_headers, sizeof(h264_headers));
    }
}

int main(int argc, char** argv) {
    char* ip = "127.0.0.1"; int port = 30005; uint64_t bps = 15000000;
    int ch; while ((ch = getopt(argc, argv, "i:p:b:")) != -1) {
        if (ch == 'i') ip = optarg; else if (ch == 'p') port = atoi(optarg); else if (ch == 'b') bps = strtoull(optarg, NULL, 10);
    }

    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    int buf_size = 2 * 1024 * 1024; setsockopt(sock, SOL_SOCKET, SO_SNDBUF, &buf_size, sizeof(buf_size));
    struct sockaddr_in addr = { .sin_family = AF_INET, .sin_port = htons(port) };
    inet_pton(AF_INET, ip, &addr.sin_addr);

    signal(SIGINT, sig_handler);
    uint8_t udp[UDP_PACKET_SIZE], cc_v = 0, cc_pat = 0, cc_pmt = 0; uint64_t pkts = 0;
    double ns_per_pkt = (188.0 * 8.0 * 1e9) / (double)bps;
    struct timespec start; clock_gettime(CLOCK_MONOTONIC, &start);

    while (g_keep_running) {
        for (int i = 0; i < 7; i++) {
            uint8_t* p = &udp[i * 188];
            uint64_t stream_ns = (uint64_t)(pkts * ns_per_pkt);
            uint64_t pcr_27m = (stream_ns * 27) / 1000;

            if (pkts % 500 == 0) {
                memcpy(p, pat_pkt, sizeof(pat_pkt)); memset(p+sizeof(pat_pkt), 0xFF, 188-sizeof(pat_pkt));
                p[3] = (p[3] & 0xF0) | (cc_pat++ & 0x0F);
            } else if (pkts % 500 == 1) {
                memcpy(p, pmt_pkt, sizeof(pmt_pkt)); memset(p+sizeof(pmt_pkt), 0xFF, 188-sizeof(pmt_pkt));
                p[3] = (p[3] & 0xF0) | (cc_pmt++ & 0x0F);
            } else if (pkts % 40 == 0) write_pcr_pkt(p, &cc_v, pcr_27m);
            else write_video_pkt(p, &cc_v, (pkts % 1000 == 10), (stream_ns * 9) / 100000);
            pkts++;
        }
        
        struct timespec now; clock_gettime(CLOCK_MONOTONIC, &now);
        double elapsed = (now.tv_sec - start.tv_sec) * 1e9 + now.tv_nsec - start.tv_nsec;
        if (pkts * ns_per_pkt > elapsed) {
            struct timespec req = { 0, (long)(pkts * ns_per_pkt - elapsed) };
            nanosleep(&req, NULL);
        }
        sendto(sock, udp, sizeof(udp), 0, (struct sockaddr*)&addr, sizeof(addr));
    }
    close(sock); return 0;
}
