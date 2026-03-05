/*
 * tsa_generator.c - Professional High-Performance TS Stream Generator
 * 
 * This tool generates a standards-compliant MPEG-TS stream with:
 * - High-precision bitrate pacing (up to 1Gbps)
 * - Periodic PAT/PMT/PCR insertion
 * - Simulated H.264 Video payload (SPS/PPS/IDR)
 * - CEA-708 Closed Caption triggers (SEI GA94)
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <getopt.h>
#include <fcntl.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <time.h>
#include <signal.h>
#include <stdbool.h>
#include <math.h>

#define TS_PACKET_SIZE 188
#define UDP_PACKET_SIZE (7 * TS_PACKET_SIZE)
#define DEFAULT_BITRATE (20 * 1000000) // 20 Mbps
#define VIDEO_PID 0x0100
#define PCR_PID   0x0100

static volatile sig_atomic_t keep_running = 1;

void sig_handler(int sig) {
    (void)sig;
    keep_running = 0;
}

/* --- H.264 Simulation Data --- */
static uint8_t h264_headers[] = {
    0x00, 0x00, 0x00, 0x01, 0x67, 0x42, 0x00, 0x1f, 0x95, 0xa8, 0x1e, 0x00, 0x5b, 0x90, // SPS (720p)
    0x00, 0x00, 0x00, 0x01, 0x68, 0xce, 0x3c, 0x80,                                     // PPS
    0x00, 0x00, 0x00, 0x01, 0x06, 0x04, 0x08, 'G',  'A',  '9',  '4',  0x03, 0x00, 0x00, // SEI CC
    0x00, 0x00, 0x00, 0x01, 0x65, 0x00, 0x00, 0x00, 0x01                                // IDR Frame
};

static uint8_t pat_pkt[TS_PACKET_SIZE] = {
    0x47, 0x40, 0x00, 0x10, 0x00, 0x00, 0xb0, 0x0d, 0x00, 0x01, 0xc1, 0x00, 0x00, 0x00, 0x01, 0xe1,
    0x00, 0x24, 0xac, 0x48, 0xbd
};

static uint8_t pmt_pkt[TS_PACKET_SIZE] = {
    0x47, 0x41, 0x00, 0x10, 0x00, 0x02, 0xb0, 0x17, 0x00, 0x01, 0xc1, 0x00, 0x00, 0xe1, 0x00, 0xf0,
    0x00, 0x1b, 0xe1, 0x00, 0xf0, 0x00, 0x4e, 0x59, 0x3d, 0x1e
};

/* --- Utilities --- */
static uint64_t get_time_ns() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + ts.tv_nsec;
}

static void insert_pcr(uint8_t* pkt, uint8_t cc, uint64_t pcr_27mhz) {
    memset(pkt, 0xFF, TS_PACKET_SIZE);
    pkt[0] = 0x47;
    pkt[1] = 0x40 | ((PCR_PID >> 8) & 0x1F);
    pkt[2] = PCR_PID & 0xFF;
    pkt[3] = 0x20 | (cc & 0x0F); // Adaption field only
    pkt[4] = 183;
    pkt[5] = 0x10; // PCR flag set
    
    uint64_t pcr_base = pcr_27mhz / 300;
    uint16_t pcr_ext = pcr_27mhz % 300;
    
    pkt[6] = (pcr_base >> 25) & 0xFF;
    pkt[7] = (pcr_base >> 17) & 0xFF;
    pkt[8] = (pcr_base >> 9) & 0xFF;
    pkt[9] = (pcr_base >> 1) & 0xFF;
    pkt[10] = ((pcr_base & 0x01) << 7) | 0x7E | ((pcr_ext >> 8) & 0x01);
    pkt[11] = pcr_ext & 0xFF;
}

static void insert_video(uint8_t* pkt, uint8_t cc, bool is_start) {
    memset(pkt, 0xFF, TS_PACKET_SIZE);
    pkt[0] = 0x47;
    pkt[1] = (is_start ? 0x40 : 0x00) | ((VIDEO_PID >> 8) & 0x1F);
    pkt[2] = VIDEO_PID & 0xFF;
    pkt[3] = 0x10 | (cc & 0x0F); // Payload only
    
    if (is_start) {
        pkt[4] = 0x00; pkt[5] = 0x00; pkt[6] = 0x01; pkt[7] = 0xe0; // PES Header
        pkt[8] = 0x00; pkt[9] = 0x00; // Length (unspecified)
        pkt[10] = 0x80; pkt[11] = 0x00; pkt[12] = 0x00; // Minimal PES flags
        memcpy(pkt + 13, h264_headers, sizeof(h264_headers));
    }
}

void print_usage(const char* prog) {
    printf("Usage: %s [options]\n", prog);
    printf("  -i, --ip <ip>       Destination IP address\n");
    printf("  -p, --port <port>   Destination UDP port\n");
    printf("  -b, --bitrate <bps> Target bitrate in bps (default: 20M)\n");
    printf("  -h, --help          Show this help\n");
}

int main(int argc, char* argv[]) {
    char* dest_ip = NULL;
    int port = 0;
    uint64_t target_bps = DEFAULT_BITRATE;

    static struct option long_options[] = {
        {"ip", required_argument, 0, 'i'},
        {"port", required_argument, 0, 'p'},
        {"bitrate", required_argument, 0, 'b'},
        {"help", no_argument, 0, 'h'},
        {0, 0, 0, 0}
    };

    int opt;
    while ((opt = getopt_long(argc, argv, "i:p:b:h", long_options, NULL)) != -1) {
        switch (opt) {
            case 'i': dest_ip = optarg; break;
            case 'p': port = atoi(optarg); break;
            case 'b': target_bps = strtoull(optarg, NULL, 10); break;
            case 'h': print_usage(argv[0]); return 0;
            default: return 1;
        }
    }

    if (!dest_ip || port == 0) {
        fprintf(stderr, "Error: IP and Port are required.\n");
        print_usage(argv[0]);
        return 1;
    }

    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    int sndbuf = 2097152;
    setsockopt(sock, SOL_SOCKET, SO_SNDBUF, &sndbuf, sizeof(sndbuf));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    inet_pton(AF_INET, dest_ip, &addr.sin_addr);

    signal(SIGINT, sig_handler);
    signal(SIGTERM, sig_handler);

    printf("Starting TSA Generator -> %s:%d @ %lu bps\n", dest_ip, port, target_bps);

    uint8_t udp_buf[UDP_PACKET_SIZE];
    uint8_t cc_v = 0, cc_pcr = 0, cc_pat = 0, cc_pmt = 0;
    uint64_t pkts_sent = 0;
    uint64_t start_ns = get_time_ns();
    
    double pps = (double)target_bps / (8.0 * UDP_PACKET_SIZE);
    double ns_per_udp = 1e9 / pps;
    uint64_t next_send_ns = start_ns;

    while (keep_running) {
        for (int i = 0; i < 7; i++) {
            uint8_t* p = &udp_buf[i * TS_PACKET_SIZE];
            
            if (pkts_sent % 1000 == 0) { // PAT every 1000 pkts
                memcpy(p, pat_pkt, 32); memset(p + 32, 0xFF, 188 - 32);
                p[3] = (p[3] & 0xF0) | (cc_pat++ & 0x0F);
            } else if (pkts_sent % 1000 == 1) { // PMT
                memcpy(p, pmt_pkt, 32); memset(p + 32, 0xFF, 188 - 32);
                p[3] = (p[3] & 0xF0) | (cc_pmt++ & 0x0F);
            } else if (pkts_sent % 40 == 0) { // PCR every 40 pkts (~40ms @ 15Mbps)
                uint64_t now_ns = get_time_ns();
                uint64_t pcr_val = ((now_ns - start_ns) * 27) / 1000;
                insert_pcr(p, cc_pcr++, pcr_val);
            } else {
                // Video payload, start a new "frame" every 1000 packets
                insert_video(p, cc_v++, (pkts_sent % 1000 == 10));
            }
            pkts_sent++;
        }

        uint64_t now = get_time_ns();
        if (now < next_send_ns) {
            uint32_t sleep_ns = next_send_ns - now;
            if (sleep_ns > 50000) {
                struct timespec req = {0, sleep_ns};
                nanosleep(&req, NULL);
            }
        } else if (now > next_send_ns + 100000000ULL) {
            next_send_ns = now;
        }

        if (sendto(sock, udp_buf, sizeof(udp_buf), 0, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
            perror("sendto");
            break;
        }
        next_send_ns += (uint64_t)ns_per_udp;
    }

    close(sock);
    printf("\nGenerator finished. Sent %lu packets.\n", pkts_sent);
    return 0;
}
