/*
 * tsa_iat_tester.c - Pure Network & CPU Scheduling Jitter Tester
 * 
 * Sends 7x188 null TS packets over UDP at precisely controlled intervals
 * to measure baseline OS scheduling and network layer jitter, completely
 * independent of TS payload parsing logic.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <getopt.h>
#include <fcntl.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <time.h>
#include <signal.h>

#define DEFAULT_SLEEP_US 1000
#define TS_PACKET_SIZE 188
#define UDP_PACKET_SIZE (7 * TS_PACKET_SIZE)

static volatile sig_atomic_t keep_running = 1;

void sig_handler(int sig) {
    (void)sig;
    keep_running = 0;
}

void print_usage(const char* prog) {
    printf("Usage: %s [options]\n", prog);
    printf("  -i, --ip <ip>       Destination IP address\n");
    printf("  -p, --port <port>   Destination UDP port\n");
    printf("  -s, --sleep <us>    Sleep interval in microseconds (default: %d)\n", DEFAULT_SLEEP_US);
    printf("  -c, --count <num>   Number of packets to send (default: 0 = infinite)\n");
    printf("  -h, --help          Show this help\n");
}

int main(int argc, char* argv[]) {
    char* dest_ip = NULL;
    int port = 0;
    int sleep_us = DEFAULT_SLEEP_US;
    unsigned long max_count = 0;

    static struct option long_options[] = {
        {"ip", required_argument, 0, 'i'},
        {"port", required_argument, 0, 'p'},
        {"sleep", required_argument, 0, 's'},
        {"count", required_argument, 0, 'c'},
        {"help", no_argument, 0, 'h'},
        {0, 0, 0, 0}
    };

    int opt;
    int option_index = 0;
    while ((opt = getopt_long(argc, argv, "i:p:s:c:h", long_options, &option_index)) != -1) {
        switch (opt) {
            case 'i': dest_ip = optarg; break;
            case 'p': port = atoi(optarg); break;
            case 's': sleep_us = atoi(optarg); break;
            case 'c': max_count = strtoul(optarg, NULL, 10); break;
            case 'h':
            default:
                print_usage(argv[0]);
                return 1;
        }
    }

    if (!dest_ip || port == 0) {
        fprintf(stderr, "Error: Destination IP and Port are required.\n");
        print_usage(argv[0]);
        return 1;
    }

    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        perror("socket");
        return 1;
    }

    struct sockaddr_in dest_addr;
    memset(&dest_addr, 0, sizeof(dest_addr));
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(port);
    if (inet_pton(AF_INET, dest_ip, &dest_addr.sin_addr) <= 0) {
        perror("inet_pton");
        close(sock);
        return 1;
    }

    // Prepare a buffer with 7 null TS packets
    uint8_t buf[UDP_PACKET_SIZE];
    memset(buf, 0xFF, sizeof(buf));
    for (int i = 0; i < 7; i++) {
        buf[i * TS_PACKET_SIZE + 0] = 0x47; // Sync byte
        buf[i * TS_PACKET_SIZE + 1] = 0x1F; // PID 0x1FFF
        buf[i * TS_PACKET_SIZE + 2] = 0xFF;
        buf[i * TS_PACKET_SIZE + 3] = 0x10; // Payload only, CC=0
    }

    struct sigaction sa;
    sa.sa_handler = sig_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    printf("Starting tsa_iat_tester -> %s:%d, sleep: %dus, max_count: %lu\n", dest_ip, port, sleep_us, max_count);

    unsigned long sent = 0;
    while (keep_running) {
        ssize_t bytes_sent = sendto(sock, buf, sizeof(buf), 0, (struct sockaddr*)&dest_addr, sizeof(dest_addr));
        if (bytes_sent < 0) {
            perror("sendto");
            break;
        }
        sent++;

        if (max_count > 0 && sent >= max_count) {
            break;
        }

        if (sleep_us > 0) {
            struct timespec req;
            req.tv_sec = sleep_us / 1000000;
            req.tv_nsec = (sleep_us % 1000000) * 1000;
            nanosleep(&req, NULL);
        }
    }

    printf("\ntsa_iat_tester finished. Sent %lu packets.\n", sent);
    close(sock);
    return 0;
}
