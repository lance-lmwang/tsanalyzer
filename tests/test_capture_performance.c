#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "tsa.h"

#define PKT_COUNT 1000000

uint64_t get_ns() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + ts.tv_nsec;
}

void benchmark(bool enable_forensics) {
    tsa_config_t cfg = {0};

    tsa_handle_t* h = tsa_create(&cfg);
    assert(h != NULL);

    uint8_t pkt[188];
    memset(pkt, 0, 188);
    pkt[0] = 0x47;

    uint64_t start = get_ns();
    for (int i = 0; i < PKT_COUNT; i++) {
        tsa_process_packet(h, pkt, (uint64_t)i);
    }
    uint64_t end = get_ns();

    double avg_ns = (double)(end - start) / PKT_COUNT;
    printf("Forensics %s: Avg process time: %.2f ns\n", enable_forensics ? "ENABLED" : "DISABLED", avg_ns);

    tsa_destroy(h);
    if (enable_forensics) unlink("forensic.ts");
}

int main() {
    printf("Running Capture Performance Benchmark...\n");
    benchmark(false);
    benchmark(true);
    return 0;
}
