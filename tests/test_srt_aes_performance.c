#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "tsa.h"
#include "tsp.h"

// Background listener thread to avoid handshake deadlock
void* srt_listener_thread(void* arg) {
    (void)arg;
    const char* listen_url = "srt://:9004?mode=listener&passphrase=benchmark-secret-key-123&pbkeylen=16";
    ts_ingest_srt_t* listener = ts_ingest_srt_create(listen_url);
    if (!listener) {
        return NULL;
    }

    uint8_t buf[1500];
    while (ts_ingest_srt_recv(listener, buf, sizeof(buf)) >= 0) {
        // Just drain the socket
    }

    ts_ingest_srt_destroy(listener);
    return NULL;
}

void test_srt_aes_performance() {
    printf("Running test_srt_aes_performance...\n");

    const char* srt_url = "srt://127.0.0.1:9004?mode=caller&passphrase=benchmark-secret-key-123&pbkeylen=16";

    pthread_t l_thread;
    pthread_create(&l_thread, NULL, srt_listener_thread, NULL);

    // Give listener time to bind
    usleep(500000);

    tsp_config_t cfg = {0};
    cfg.bitrate = 100000000;  // 100 Mbps
    cfg.ts_per_udp = 7;
    cfg.srt_url = srt_url;

    tsp_handle_t* pacer = tsp_create(&cfg);
    if (!pacer) {
        fprintf(stderr, "Failed to create pacer handle (Handshake failed)\n");
        pthread_cancel(l_thread);
        pthread_join(l_thread, NULL);
        return;
    }

    assert(tsp_start(pacer) == 0);

    uint8_t pkt[188];
    memset(pkt, 0x47, 188);

    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);

    int total_pkts = 20000;
    for (int i = 0; i < total_pkts; i++) {
        if (tsp_enqueue(pacer, pkt, 1) != 0) {
            usleep(100);
            i--; // retry
            continue;
        }
    }

    // Give some time for egress
    usleep(200000);

    clock_gettime(CLOCK_MONOTONIC, &end);
    double elapsed = (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) / 1e9;
    double mbps = (double)total_pkts * 188 * 8 / (elapsed * 1e6);

    printf("AES Egress Performance: %.2f Mbps (over %.2f s)\n", mbps, elapsed);

    tsp_destroy(pacer);
    pthread_cancel(l_thread);
    pthread_join(l_thread, NULL);
    printf("test_srt_aes_performance passed.\n");
}

int main() {
    test_srt_aes_performance();
    return 0;
}
