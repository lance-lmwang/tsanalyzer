#define _GNU_SOURCE
#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include "tsa.h"

#define TEST_PORT 9999
#define TOTAL_PACKETS 500000

static uint64_t get_now_ns() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + ts.tv_nsec;
}

// Mock real-time analysis thread
void* analysis_worker(void* arg) {
    tsa_handle_t* h = (tsa_handle_t*)arg;
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(TEST_PORT);
    addr.sin_addr.s_addr = INADDR_ANY;

    int rbuf = 10 * 1024 * 1024;
    setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &rbuf, sizeof(rbuf));

    if (bind(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind failed");
        return NULL;
    }

    uint8_t buffer[65535];
    while (1) {
        ssize_t len = recv(fd, buffer, sizeof(buffer), 0);
        if (len <= 0) break;
        uint64_t now = get_now_ns();
        for (int i = 0; i + 188 <= len; i += 188) {
            tsa_process_packet(h, buffer + i, now);
        }
    }
    close(fd);
    return NULL;
}

int main() {
    tsa_config_t cfg = {.is_live = true, .analysis.pcr_ema_alpha = 0.01};
    tsa_handle_t* h = tsa_create(&cfg);

    pthread_t tid;
    pthread_create(&tid, NULL, analysis_worker, h);
    usleep(100000);  // Wait for receiving thread to be ready

    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in addr = {
        .sin_family = AF_INET, .sin_port = htons(TEST_PORT), .sin_addr.s_addr = inet_addr("127.0.0.1")};

    printf(">>> UDP Stability Stress Test: %d packets...\n", TOTAL_PACKETS);

    uint8_t pkt[188];
    memset(pkt, 0xFF, 188);
    pkt[0] = 0x47;
    pkt[1] = 0x00;
    pkt[2] = 0x11;  // PID 0x11

    int lost_count = 0;
    for (int i = 0; i < TOTAL_PACKETS; i++) {
        pkt[3] = 0x10 | (i % 16);

        // Mock random packet loss (0.05%)
        if (rand() % 2000 == 0) {
            lost_count++;
            continue;
        }

        if (sendto(fd, pkt, 188, 0, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
            if (errno != EAGAIN && errno != EWOULDBLOCK) perror("sendto");
        }

        // Mock high-frequency small bursts, no usleep for stress testing
        if (i % 1000 == 0) {
            // Allow receiver to process
            __builtin_ia32_pause();
        }
    }

    printf("Simulation finished. Waiting for analyzer to catch up...\n");
    sleep(2);

    tsa_snapshot_full_t fstats;
    tsa_commit_snapshot(h, get_now_ns());
    tsa_take_snapshot_full(h, &fstats);
    tsa_tr101290_stats_t stats = fstats.stats;

    printf("\n[UDP Stability Report]\n");
    printf("------------------------------------------\n");
    printf("Packets Processed : %lu\n", stats.total_ts_packets);
    printf("CC Errors Found   : %lu\n", stats.cc_error.count);
    printf("Simulated Loss    : %d\n", lost_count);
    printf("Sync Byte Errors  : %lu\n", stats.sync_byte_error.count);
    printf("Accuracy          : %.2f%%\n",
           (1.0 - (double)abs((int)stats.cc_error.count - lost_count) / TOTAL_PACKETS) * 100.0);
    printf("Status            : %s\n", (stats.total_ts_packets > 0 && stats.cc_error.count > 0) ? "STABLE" : "FAIL");
    printf("------------------------------------------\n");

    pthread_cancel(tid);
    pthread_join(tid, NULL);
    tsa_destroy(h);
    close(fd);
    return 0;
}
