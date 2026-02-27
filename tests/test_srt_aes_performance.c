#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "tsa.h"
#include "tsp.h"

void test_srt_aes_performance() {
    printf("Running test_srt_aes_performance...\n");

    const char* srt_url = "srt://127.0.0.1:9004?mode=caller&passphrase=benchmark-secret-key-123&pbkeylen=16";
    
    tsp_config_t cfg = {0};
    cfg.bitrate = 100000000; // 100 Mbps
    cfg.ts_per_udp = 7;
    cfg.srt_url = srt_url;

    // Start a dummy listener to accept connection
    ts_ingest_srt_t* listener = ts_ingest_srt_create("srt://:9004?mode=listener&passphrase=benchmark-secret-key-123&pbkeylen=16");
    assert(listener != NULL);

    tsp_handle_t* pacer = tsp_create(&cfg);
    assert(pacer != NULL);
    assert(tsp_start(pacer) == 0);

    uint8_t pkt[188];
    memset(pkt, 0x47, 188);

    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);

    int total_pkts = 100000;
    for (int i = 0; i < total_pkts; i++) {
        tsp_enqueue(pacer, pkt, 1);
        if (i % 1000 == 0) usleep(100);
    }

    clock_gettime(CLOCK_MONOTONIC, &end);
    double elapsed = (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) / 1e9;
    double mbps = (double)total_pkts * 188 * 8 / (elapsed * 1e6);

    printf("AES Egress Performance: %.2f Mbps (over %.2f s)\n", mbps, elapsed);

    tsp_destroy(pacer);
    ts_ingest_srt_destroy(listener);
    printf("test_srt_aes_performance passed.\n");
}

int main() {
    test_srt_aes_performance();
    return 0;
}
